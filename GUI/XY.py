import sys
import time
import serial
import serial.tools.list_ports
from collections import deque
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QPushButton, QLabel, QLineEdit, QComboBox, QGroupBox, QFormLayout)
from PyQt6.QtCore import pyqtSignal, QThread, QTimer
from PyQt6.QtGui import QFont
import pyqtgraph as pg

class SerialThread(QThread):
    """Wątek odpowiedzialny za odbieranie danych z STM32"""
    data_received = pyqtSignal(str)

    def __init__(self, serial_conn):
        super().__init__()
        self.serial_conn = serial_conn
        self.running = True

    def run(self):
        while self.running and self.serial_conn.is_open:
            try:
                if self.serial_conn.in_waiting > 0:
                    line = self.serial_conn.readline().decode('utf-8').strip()
                    if line:
                        self.data_received.emit(line)
                else:
                    time.sleep(0.005)
            except Exception as e:
                    print(f"Błąd odbioru: {e}")
                    break

    def stop(self):
        self.running = False
        

class TestAppX(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Tester 2D - Cyfrowy Bliźniak (Osie X i Y)")
        self.ser = serial.Serial()
        self.ser.baudrate = 115200
        
        # --- ZAAWANSOWANE STAŁE KINEMATYCZNE ---
        
        # OŚ X (Skalibrowane)
        self.ENCODER_STEPS_PER_MM_X = 24.998  
        self.MOTOR_STEPS_PER_MM_X = 33.535  
        
        # OŚ Y (Tymczasowe - do zaktualizowania po kalibracji fizycznej!)
        self.ENCODER_STEPS_PER_MM_Y = 173.374 
        self.MOTOR_STEPS_PER_MM_Y = 28.955
        
        # Zmienne stanu
        self.target_x_mm = 0.0
        self.target_y_mm = 0.0
        self.actual_x_mm = 0.0
        self.actual_y_mm = 0.0
        self.raw_enc_x = 0
        self.raw_enc_y = 0
        
        # Bufory do wykresu 2D (pamiętają ostatnie 500 punktów rysując "ślad" wózka)
        self.buffer_size = 500
        self.path_x = deque(maxlen=self.buffer_size)
        self.path_y = deque(maxlen=self.buffer_size)

        self.init_ui()
        
        # Timer odświeżania GUI (50ms = 20 FPS)
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_gui)
        self.timer.start(50)

    def init_ui(self):
        main_widget = QWidget()
        layout = QHBoxLayout()
        
        # --- LEWY PANEL: STEROWANIE I ODCZYTY ---
        left_panel = QVBoxLayout()
        
        # 1. Połączenie
        conn_group = QGroupBox("Połączenie UART")
        conn_layout = QVBoxLayout()
        self.port_combo = QComboBox()
        self.refresh_ports()
        conn_layout.addWidget(self.port_combo)
        
        btn_layout = QHBoxLayout()
        self.conn_btn = QPushButton("Połącz")
        self.conn_btn.clicked.connect(self.toggle_connection)
        self.refresh_btn = QPushButton("Odśwież")
        self.refresh_btn.clicked.connect(self.refresh_ports)
        btn_layout.addWidget(self.conn_btn)
        btn_layout.addWidget(self.refresh_btn)
        conn_layout.addLayout(btn_layout)
        conn_group.setLayout(conn_layout)
        left_panel.addWidget(conn_group)
        
        # 2. Sterowanie
        ctrl_group = QGroupBox("Sterowanie Robotem (X, Y)")
        ctrl_layout = QVBoxLayout()
        
        form_layout = QFormLayout()
        self.input_x = QLineEdit("0")
        self.input_y = QLineEdit("0")
        form_layout.addRow("Cel X [mm]:", self.input_x)
        form_layout.addRow("Cel Y [mm]:", self.input_y)
        ctrl_layout.addLayout(form_layout)
        
        self.move_btn = QPushButton("IDŹ DO PUNKTU")
        self.move_btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold; padding: 10px;")
        self.move_btn.clicked.connect(self.send_move)
        ctrl_layout.addWidget(self.move_btn)
        
        self.home_btn = QPushButton("HOMING (Bazuj obie osie)")
        self.home_btn.clicked.connect(lambda: self.send_raw("H:0:0"))
        ctrl_layout.addWidget(self.home_btn)
        
        self.calib_btn = QPushButton("AUTO-KALIBRACJA (Szukaj MAX)")
        self.calib_btn.setStyleSheet("background-color: #2196F3; color: white; font-weight: bold;")
        self.calib_btn.clicked.connect(lambda: self.send_raw("C:0:0"))
        ctrl_layout.addWidget(self.calib_btn)
        
        self.stop_btn = QPushButton("E-STOP (Zatrzymaj)")
        self.stop_btn.setStyleSheet("background-color: #f44336; color: white; font-weight: bold; padding: 10px;")
        self.stop_btn.clicked.connect(lambda: self.send_raw("S:0:0"))
        ctrl_layout.addWidget(self.stop_btn)
        
        self.reset_btn = QPushButton("RESET ALARMU")
        self.reset_btn.clicked.connect(lambda: self.send_raw("R:0:0"))
        ctrl_layout.addWidget(self.reset_btn)
        
        ctrl_group.setLayout(ctrl_layout)
        left_panel.addWidget(ctrl_group)
        
        # 3. Odczyty na żywo (Live Data)
        data_group = QGroupBox("Odczyty na żywo")
        data_layout = QVBoxLayout()
        font = QFont("Courier", 12, QFont.Weight.Bold)
        
        # Oś X
        self.lbl_raw_x = QLabel("Kroki X: 0")
        self.lbl_raw_x.setFont(font)
        self.lbl_calc_x = QLabel("Poz X: 0.00 mm")
        self.lbl_calc_x.setFont(font)
        self.lbl_calc_x.setStyleSheet("color: #d32f2f;")
        
        # Oś Y
        self.lbl_raw_y = QLabel("Kroki Y: 0")
        self.lbl_raw_y.setFont(font)
        self.lbl_calc_y = QLabel("Poz Y: 0.00 mm")
        self.lbl_calc_y.setFont(font)
        self.lbl_calc_y.setStyleSheet("color: #1976D2;")
        
        data_layout.addWidget(self.lbl_raw_x)
        data_layout.addWidget(self.lbl_calc_x)
        data_layout.addWidget(QLabel("-" * 20)) # Separator
        data_layout.addWidget(self.lbl_raw_y)
        data_layout.addWidget(self.lbl_calc_y)
        
        data_group.setLayout(data_layout)
        left_panel.addWidget(data_group)
        
        left_panel.addStretch()
        layout.addLayout(left_panel, 1)

        # --- PRAWY PANEL: WYKRES PRZESTRZENNY 2D ---
        self.plot_widget = pg.PlotWidget()
        self.plot_widget.setBackground('w')
        self.plot_widget.setTitle("Cyfrowy Bliźniak - Przestrzeń Robocza XY", color="k", size="12pt")
        
        self.plot_widget.setLabel('left', 'Oś Y', units='mm', color='k')
        self.plot_widget.setLabel('bottom', 'Oś X', units='mm', color='k')
        self.plot_widget.showGrid(x=True, y=True, alpha=0.5)
        self.plot_widget.addLegend()
        
        # KLUCZOWE: Odwrócenie osi Y (Punkt 0,0 jest w lewym górnym rogu)
        self.plot_widget.getPlotItem().invertY(True)

        # --- NOWE: SZTYWNE RAMY OBSZARU ROBOCZEGO ---
        # Ustawiamy sztywne limity na podstawie fizycznej wielkości ramy
        self.plot_widget.setXRange(0, 650, padding=0)
        self.plot_widget.setYRange(0, 291.9, padding=0)
        
        # Wyłączamy możliwość przesuwania i przybliżania wykresu myszką
        self.plot_widget.setMouseEnabled(x=False, y=False)
        
        # Ukrywamy przycisk "A" (Auto-Range), który pojawia się w rogu
        self.plot_widget.hideButtons()

        # Rysowanie rzeczywistej ścieżki i punktu celu
        pen_actual = pg.mkPen(color=(255, 0, 0), width=2) 
        self.line_actual = self.plot_widget.plot([], [], pen=pen_actual, name="Fizyczna ścieżka (Enkoder)")
        
        # Cel (krzyżyk)
        self.point_target = self.plot_widget.plot([], [], pen=None, symbol='+', symbolSize=15, 
                                                  symbolPen=pg.mkPen('b', width=3), name="Cel zadany")
        
        layout.addWidget(self.plot_widget, 3)
        
        main_widget.setLayout(layout)
        self.setCentralWidget(main_widget)
        self.resize(1200, 700)

    def refresh_ports(self):
        self.port_combo.clear()
        ports = [p.device for p in serial.tools.list_ports.comports()]
        if not ports:
            self.port_combo.addItem("Brak urządzeń")
        else:
            self.port_combo.addItems(ports)

    def toggle_connection(self):
        if not self.ser.is_open:
            try:
                port = self.port_combo.currentText()
                if port == "Brak urządzeń": return
                
                self.ser.port = port
                self.ser.timeout = 0.1   
                self.ser.open()
                self.conn_btn.setText("Rozłącz")
                self.conn_btn.setStyleSheet("background-color: #ff9800; color: black; font-weight: bold;")
                
                # Czyszczenie buforów rysowania
                self.path_x.clear()
                self.path_y.clear()
                
                self.serial_thread = SerialThread(self.ser)
                self.serial_thread.data_received.connect(self.handle_serial_data)
                self.serial_thread.start()
            except Exception as e:
                print(f"Błąd: {e}")
        else:
            self.serial_thread.stop()
            self.ser.close()
            self.conn_btn.setText("Połącz")
            self.conn_btn.setStyleSheet("")

    def send_move(self):
        try:
            val_x = float(self.input_x.text().replace(',', '.'))
            val_y = float(self.input_y.text().replace(',', '.'))
            
            self.target_x_mm = val_x
            self.target_y_mm = val_y
            
            # Przeliczanie milimetrów na kroki silników (KOMENDY)
            steps_x = int(self.target_x_mm * self.MOTOR_STEPS_PER_MM_X)
            steps_y = int(self.target_y_mm * self.MOTOR_STEPS_PER_MM_Y)
            
            self.send_raw(f"M:{steps_x}:{steps_y}")
        except ValueError:
            print("Błąd: Nieprawidłowa wartość docelowa X lub Y!")

    def send_raw(self, msg):
        if self.ser.is_open:
            self.ser.write(f"{msg}\n".encode('utf-8'))

    def handle_serial_data(self, data):
        # 1. Rutynowa telemetria (P:X:Y)
        if data.startswith("P:"):
            try:
                parts = data.split(":")
                if len(parts) >= 3:
                    self.raw_enc_x = int(parts[1])
                    self.raw_enc_y = int(parts[2])
                    
                    self.actual_x_mm = self.raw_enc_x / self.ENCODER_STEPS_PER_MM_X
                    self.actual_y_mm = self.raw_enc_y / self.ENCODER_STEPS_PER_MM_Y
            except ValueError:
                pass 
                
        # 2. Potwierdzenie z maszyny (OK)
        elif data.startswith("OK:"):
            print(f"[{time.strftime('%H:%M:%S')}] Ruch zakończony. Pozycja docelowa osiągnięta.")
                
        # 3. Odczyty kalibracyjne osi X i Y
        elif data.startswith("CAL_X:"):
            print(f"\n--- WYNIK KALIBRACJI OSI X ---")
            print(f"Otrzymano: {data}")
            print(f"Zanotuj te liczby i podziel przez zmierzoną długość osi X w mm!")
            
        elif data.startswith("CAL_Y:"):
            print(f"\n--- WYNIK KALIBRACJI OSI Y ---")
            print(f"Otrzymano: {data}")
            print(f"Zanotuj te liczby i podziel przez zmierzoną długość osi Y w mm!")

        # 4. Obsługa błędu
        elif data.startswith("ERR:"):
            error_msg = data.split(":", 1)[1] if ":" in data else data
            print(f"[{time.strftime('%H:%M:%S')}] BŁĄD SYSTEMU: {error_msg}")

    def update_gui(self):
        # Aktualizacja etykiet
        self.lbl_raw_x.setText(f"Kroki X: {self.raw_enc_x}")
        self.lbl_calc_x.setText(f"Poz X: {self.actual_x_mm:.2f} mm")
        
        self.lbl_raw_y.setText(f"Kroki Y: {self.raw_enc_y}")
        self.lbl_calc_y.setText(f"Poz Y: {self.actual_y_mm:.2f} mm")
        
        # Aktualizacja punktu docelowego na wykresie
        self.point_target.setData([self.target_x_mm], [self.target_y_mm])
        
        # Aktualizacja rysowanej ścieżki (jeśli port otwarty)
        if self.ser.is_open:
            self.path_x.append(self.actual_x_mm)
            self.path_y.append(self.actual_y_mm)
            self.line_actual.setData(list(self.path_x), list(self.path_y))
            
    def closeEvent(self, event):      
        if hasattr(self, 'serial_thread'):
            self.serial_thread.stop()
            self.serial_thread.wait(1000)
        if self.ser.is_open:
            self.ser.close()
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion") 
    window = TestAppX()
    window.show()
    sys.exit(app.exec())