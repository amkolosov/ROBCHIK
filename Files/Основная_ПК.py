import cv2
import time
import requests
import numpy as np
import tkinter as tk
from ultralytics import YOLO
from tkinter import messagebox
from PIL import Image, ImageTk
from multiprocessing import Process, Queue, Event

model = YOLO("yolov8n-cls.pt")


result_text = ["Робчик. Обходчик турбинного оборудования атомной станции. Результат осмотра:","Локация №1: Обнаружена утечка вспомогательного паропровода", "Локация №2: уровень шума в норме", "Локация №3: всё в норме", "Локация №4: Обнаружено возгорание. Вызвана опер группа", "Локация №5: уровень вибрации в норме"]


# ----------------------------------------------------------------------------------------------------------Функция отпраки сообщения для Arduina---------------------------------------------------------------------------------------------------------------
def send_bait(cammand):
    print(f"Попытка отправки команды: {cammand}")
    try:
        response = requests.post(URL_FREENOVE_2, data=str(cammand), timeout=3)  # Увеличили таймаут до 3 сек
        if response.status_code == 200:
            print(f"command is good to go: {cammand}")
            return True
        else:
            print(f"error server: {response.status_code}")
            return False
    except requests.exceptions.RequestException as e:
        print(f"Предупреждение: Не удалось отправить команду {cammand} (Плата еще не готова или занята): {e}")
        return False


# ------------------------------------------------------------------------------------------------------------Функция распознования объекта----------------------------------------------------------------------------------------------------------------------------------------
ras = "none:0.0"  # Инициализируем глобальную переменную дефолтным значением


def process_image_2(img):
    global ras
    results = model(img)[0]
    if results.probs is not None:
        top1_idx = results.probs.top1
        top1_conf = results.probs.top1conf.item()
        class_name = results.names[top1_idx]
        label = f'{class_name}:{top1_conf:.2f}'
        cv2.putText(img, label, (30, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 0, 0), 2)
        ras = label
    return img  # Возвращаем измененную входящую картинку img


# -----------------------------------------------------------------------------------------------------------------Параметры арукомаркеркеров-------------------------------------------------------------------------------------------------------
aruco_dict = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_250)
aruco_params = cv2.aruco.DetectorParameters()
# Минимальный размер маркера в пикселях относительно кадра. По умолчанию он 0.03. Если маркер слишком мал или размыт, можно уменьшить до 0.01-0.02:
aruco_params.minMarkerPerimeterRate = 0.01
# 2. Включаем субпиксельное уточнение (высчитывает углы с точностью до доли пикселя)
aruco_params.cornerRefinementMethod = cv2.aruco.CORNER_REFINE_SUBPIX
# 3. Делаем более детальной. Помогает при неравномерном освещении, если одна половина маркера в тени
aruco_params.adaptiveThreshWinSizeMin = 4
aruco_params.adaptiveThreshWinSizeMax = 30
aruco_params.adaptiveThreshWinSizeStep = 5
# 4. Снижаем строгие требования к идеальности квадрата
aruco_params.polygonalApproxAccuracyRate = 0.05
# Создаем финальный детектор
detector = cv2.aruco.ArucoDetector(aruco_dict, aruco_params)

# ----------------------------------------------------------------------------------------------------Функция получения картинки с видео потока камеры--------------------------------------------------------
URL_FREENOVE = "http://192.168.4.1/capture"# ссылка на стрим камеры фринова
URL_FREENOVE_2 = "http://192.168.4.1:80/cmd" # урл потока сообщеньки
URL_CAM = "http://192.168.4.3/capture" # ссылка на стрим камеры кам

# ------------------------------------------------------------------------------------------------------------------Функция анализа видео потока------------------------------------------------------------------------------------------------------
def analysis_worker(frame_queue_1, frame_queue_2, my_queue, stop_event):
    print("Процесс аналитики запущен.")

    # Открываем файл для записи результатов
    with open("analytics_results.txt", "w", encoding="utf-8") as f:
        while not stop_event.is_set() or not frame_queue_1.empty() or not frame_queue_2.empty():
            if not frame_queue_1.empty() and not frame_queue_2.empty():
                # Забираем кадры из очереди
                frame_cope_1 = frame_queue_1.get()
                frame_cope_2 = frame_queue_2.get()

                global result_text

                # Обработка задания с огнём
                frame_cope_2_faer = process_image_2(frame_cope_2)
                if ':' in ras:
                    raspozn = ras.split(':')[0] + ':'
                else:
                    raspozn = ""

                if raspozn == 'candle:':
                    result_text[0] += "Найден огонь!!!"
                    print("Отпровляю сообщение руководителю о запросе группы пожарных в сектор №1")

                # Обработка пути по ArUco
                corners_1, ids_1, rejected_1 = detector.detectMarkers(frame_cope_1)

                if ids_1 is not None:
                    if 1 in ids_1:
                        send_bait("1")
                        result_text[1] += "Найден огонь!!!"
                    if 2 in ids_1:
                        send_bait("2")
                    if 3 in ids_1:
                        send_bait("3")
                        print("cjj,otymrb ytn yj")
                    if 4 in ids_1:
                        send_bait("4")
                    if 5 in ids_1:
                        send_bait("5")
                    # Внутри analysis_worker для теста:
                    if rejected_1 is not None and len(rejected_1) > 0:
                        print(f"Найдено {len(rejected_1)} подозрительных контуров, но они не распознаны как ArUco")
                my_queue.put(result_text)


            else:
                time.sleep(0.001)  # Защита от холостого разгона процессора
    print("Процесс аналитики завершен и данные сохранены в тексовом документе.")

# ---------------------------------------------------------------------------------------------------------------------Функция отображения---------------------------------------------------------------------------------------------------------
def main():
    global URL_CAM, URL_FREENOVE, URL_FREENOVE_2
    # Очередь для передачи кадров между процессами
    frame_queue_1 = Queue(maxsize=10)  # maxsize защищает от переполнения памяти
    frame_queue_2 = Queue(maxsize=10)
    stop_event = Event()

    my_queue = Queue()

    # Запускаем процесс анализа в фоне (передаем my_queue третьим аргументом)
    analyzer_process = Process(target=analysis_worker, args=(frame_queue_1, frame_queue_2, my_queue, stop_event))
    analyzer_process.start()

    print("Запуск стабильного покадрового захвата...")
    session = requests.Session()
    print("Зажмите 'q' в окне с видео для выхода.")

    print("Ожидание стабилизации Wi-Fi соединения...")

    # Пытаемся отправить стартовую команду "6" несколько раз, пока плата не ответит
    for attempt in range(5):
        if send_bait("6"):
            print("Стартовая команда '6' успешно доставлена!")
            break
        print(f"Повторная попытка {attempt + 2}/5 через 2 секунды...")
        time.sleep(2)

    root = tk.Tk()
    root.withdraw()

    top_window = tk.Toplevel(root)
    top_window.title("Результаты осмотра оборудования АЭС")
    top_window.geometry("1340x350+0+0")

    # ИСПОЛЬЗУЕМ ТЕКСТОВЫЙ ВИДЖЕТ ВМЕСТО LABEL ДЛЯ ПОДДЕРЖКИ РАЗНЫХ ЦВЕТОВ
    result_text_box = tk.Text(top_window, font=("Arial", 28), bg=top_window["bg"], bd=0, highlightthickness=0)
    result_text_box.pack(fill="both", expand=True, padx=20, pady=20)

    # Предварительная настройка стилей и цветов (Теги)
    result_text_box.tag_config("header", foreground="black",
                               font=("Arial", 30, "bold"))  # Синий жирный заголовок для Робчика
    result_text_box.tag_config("danger", foreground="red",
                               font=("Arial", 28, "bold"))  # Красный жирный для аварийных ситуаций
    result_text_box.tag_config("normal", foreground="green")  # Зеленый для штатного состояния "в норме"

    # Начальное состояние текстового поля
    result_text_box.insert(tk.END, "Ожидание данных от робота-обходчика...")
    result_text_box.config(state=tk.DISABLED)

    while True:
        try:
            # Запрашиваем кадры со стрима
            response_1 = session.get(URL_FREENOVE, timeout=1)
            response_2 = session.get(URL_CAM, timeout=1)
            # проверка количества информации (прислали фото или видио)
            if response_1.status_code == 200 and response_2.status_code == 200:
                # запоминаем фото
                img_bytes_1 = response_1.content
                img_bytes_2 = response_2.content

                # Проверяем заголовки JPEG
                is_jpeg_1 = img_bytes_1.startswith(b'\xff\xd8')
                is_jpeg_2 = img_bytes_2.startswith(b'\xff\xd8')

                if is_jpeg_1 and is_jpeg_2:
                    # преобразуем байты в картинку со стрима
                    img_np_1 = np.frombuffer(img_bytes_1, dtype=np.uint8)
                    img_np_2 = np.frombuffer(img_bytes_2, dtype=np.uint8)
                    frame_1 = cv2.imdecode(img_np_1, cv2.IMREAD_COLOR)
                    frame_2 = cv2.imdecode(img_np_2, cv2.IMREAD_COLOR)

                    if frame_1 is not None and frame_2 is not None:

                        # Отправляем кадры на анализ (если очередь полная, skip, чтобы видео не лагало)
                        if not frame_queue_1.full():
                            frame_queue_1.put((frame_1.copy()))
                        if not frame_queue_2.full():
                            frame_queue_2.put((frame_2.copy()))

                        # Неблокирующее получение данных: проверяем, пришло ли что-то из аналитики
                        if not my_queue.empty():
                            global_result_text = my_queue.get()

                            # Разрешаем редактирование виджета для обновления логов
                            result_text_box.config(state=tk.NORMAL)
                            result_text_box.delete("1.0", tk.END)  # Полностью очищаем старый текст

                            # Интеллектуально распределяем цвета по строкам из массива
                            for i, line in enumerate(global_result_text):
                                if i == 0:
                                    current_tag = "header"  # Робчик заголовок
                                elif "утечка" in line.lower() or "возгорание" in line.lower() or "повышеный уровень" in line.lower():
                                    current_tag = "danger"  # Тревога (красный)
                                else:
                                    current_tag = "normal"  # Штатно (зеленый)

                                # Вставляем текущую строку массива с нужным цветом
                                result_text_box.insert(tk.END, line + "\n", current_tag)

                            # Блокируем поле обратно для защиты от случайного ввода пользователем
                            result_text_box.config(state=tk.DISABLED)

                        # Вывод картинок со стрима в окна
                        cv2.imshow("ESP32 WROVER", frame_1)
                        cv2.imshow("ESP32 CAM", frame_2)

                        # Обновляем окно Tkinter, чтобы текст менялся на лету
                        root.update()

                else:
                    # Если если это не фото, этот принт сразу покажет, какая плата шлет не JPEG

                    print(
                        f"Ошибка формата! Камера1 JPEG: {is_jpeg_1} (Байты: {img_bytes_1[:10]}), Камера2 JPEG: {is_jpeg_2}")

            else:
                print(f"Плата 1 статус: {response_1.status_code}, Плата 2 статус: {response_2.status_code}")
                print(f"Попытка поменять ссылку")
                if URL_CAM == "http://192.168.4.2":
                    URL_CAM = "http://192.168.4.3"
                else:
                    URL_CAM = "http://192.168.4.2"

            # Выход по нажатию >> q <<
            if cv2.waitKey(1) & 0xFF == ord('q'):
                send_bait("7")  # команда о конце работы проги и конце езды
                break

            time.sleep(0.05)  # Чуть уменьшил задержку для повышения плавности

        # в смотрим с кем потеря связи
        except requests.exceptions.RequestException as e:56
            print(f"Потеря связи с Wi-Fi: {e}")

            if URL_CAM == "http://192.168.4.3/capture":
                URL_CAM = "http://192.168.4.2/capture"
            else:
                URL_CAM = "http://192.168.4.3/capture"
            time.sleep(1)

    # Корректно завершаем работу
    # Записываем данные
    f.write("\n".join(result_text))
    f.flush()  # Сохраняем на диск
    print("Закрытие потоков...")
    stop_event.set()  # Сигнализируем второму процессу, что пора заканчивать
    cv2.destroyAllWindows()

    # Ждем, пока процесс аналитики доработает оставшиеся кадры в очереди
    analyzer_process.join()
    print("Программа успешно завершена.")


# ---------------------------------------------------------------------------------------------------------------- запуск цикла ------------------------------------------------------------------------------------------------------------------------------------------------------
if __name__ == '__main__':
    main()
