import serial
import threading
import time
import json
from flask import Flask, render_template, jsonify, request
from flask_cors import CORS
import psycopg2
from datetime import datetime
import logging

# Setup logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

# --- KONFIGURASI PENTING ---
# Ganti sesuai port Arduino Anda (misal: 'COM3' di Windows, '/dev/ttyUSB0' atau '/dev/ttyACM0' di Linux)
SERIAL_PORT = '/dev/ttyUSB0'
BAUD_RATE = 9600

# Konfigurasi Database (Ganti sesuai pengaturan PostgreSQL Anda)
DB_NAME = "tandon_air_db"
DB_USER = "postgres"
DB_PASS = "123"  # Ganti dengan password PostgreSQL Anda
DB_HOST = "localhost"
DB_PORT = "5432"

# Variabel global untuk data terakhir
latest_data = {
    "water_level_cm": 0.0,
    "water_level_percent": 0.0,
    "pump_status": "OFF",
    "alert_status": "INIT",
    "control_mode": "AUTO",
    "manual_pump_state": False,
    "timestamp": datetime.now().isoformat()
}

# Lock untuk thread safety
data_lock = threading.Lock()

# Variabel untuk komunikasi serial
ser = None
serial_lock = threading.Lock()


def init_database():
    """Inisialisasi tabel database jika belum ada."""
    conn = None
    try:
        conn = psycopg2.connect(dbname='postgres', user=DB_USER, password=DB_PASS, host=DB_HOST, port=DB_PORT)
        conn.autocommit = True
        with conn.cursor() as cur:
            cur.execute(f"SELECT 1 FROM pg_database WHERE datname='{DB_NAME}'")
            if cur.fetchone() is None:
                cur.execute(f"CREATE DATABASE {DB_NAME}")
                logger.info(f"Database '{DB_NAME}' created.")
    except psycopg2.Error as e:
        logger.warning(f"Could not create database. It might already exist. Error: {e}")
    finally:
        if conn: conn.close()

    try:
        conn = psycopg2.connect(dbname=DB_NAME, user=DB_USER, password=DB_PASS, host=DB_HOST, port=DB_PORT)
        with conn.cursor() as cur:
            cur.execute("""
                CREATE TABLE IF NOT EXISTS water_level_history (
                    id SERIAL PRIMARY KEY,
                    timestamp TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
                    water_level_cm FLOAT NOT NULL,
                    water_level_percent FLOAT NOT NULL,
                    pump_status VARCHAR(10) NOT NULL,
                    alert_status VARCHAR(20) NOT NULL,
                    control_mode VARCHAR(10) DEFAULT 'AUTO',
                    manual_pump_state BOOLEAN DEFAULT FALSE
                )
            """)
            cur.execute("CREATE INDEX IF NOT EXISTS idx_water_level_timestamp ON water_level_history(timestamp DESC)")
            conn.commit()
            logger.info("Table 'water_level_history' is ready.")
    except (psycopg2.Error, Exception) as e:
        logger.error(f"Error initializing table: {e}")
        raise
    finally:
        if conn: conn.close()


def get_db_connection():
    """Membuat koneksi ke database."""
    try:
        return psycopg2.connect(dbname=DB_NAME, user=DB_USER, password=DB_PASS, host=DB_HOST, port=DB_PORT,
                                connect_timeout=10)
    except psycopg2.OperationalError as e:
        logger.error(f"Database connection failed: {e}")
        return None


def save_data_to_db(data):
    """Menyimpan data ke database."""
    conn = get_db_connection()
    if not conn: return False
    try:
        with conn.cursor() as cur:
            sql = """
                INSERT INTO water_level_history 
                (water_level_cm, water_level_percent, pump_status, alert_status, control_mode, manual_pump_state)
                VALUES (%s, %s, %s, %s, %s, %s)
            """
            cur.execute(sql, (
                float(data['water_level_cm']),
                float(data['water_level_percent']),
                str(data['pump_status']),
                str(data['alert_status']),
                str(data.get('control_mode', 'AUTO')),
                bool(data.get('manual_pump_state', False))
            ))
            conn.commit()
            return True
    except (psycopg2.Error, KeyError, ValueError) as e:
        logger.error(f"Error saving data to DB: {e}")
        return False
    finally:
        if conn: conn.close()


def validate_sensor_data(data):
    """Validasi data sensor sebelum disimpan."""
    required_fields = ['water_level_cm', 'water_level_percent', 'pump_status', 'alert_status']
    if not all(field in data for field in required_fields): return False
    try:
        float(data['water_level_percent'])
        return True
    except (ValueError, TypeError):
        return False


def send_command_to_arduino(command):
    """Mengirim perintah ke Arduino."""
    with serial_lock:
        if ser and ser.is_open:
            try:
                # Hapus spasi dari JSON dengan `separators` agar cocok dengan `indexOf` di Arduino
                command_json = json.dumps(command, separators=(',', ':')) + '\n'
                ser.write(command_json.encode('utf-8'))
                ser.flush()
                logger.info(f"Command sent to Arduino: {command_json.strip()}")
                return True
            except Exception as e:
                logger.error(f"Error sending command to Arduino: {e}")
                return False
        logger.error("Serial connection not available")
        return False


def read_from_arduino():
    """Membaca data dari Arduino."""
    global ser
    while True:
        try:
            if ser is None or not ser.is_open:
                logger.info(f"Attempting to connect to serial port {SERIAL_PORT}...")
                with serial_lock:
                    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=5)
                logger.info(f"Connected to serial port {SERIAL_PORT}")
                time.sleep(2)

            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                try:
                    data = json.loads(line)
                    if validate_sensor_data(data):
                        data['timestamp'] = datetime.now().isoformat()
                        with data_lock:
                            latest_data.update(data)
                        save_data_to_db(data)
                    elif any(key in data for key in ['response', 'error', 'pump_change', 'status']):
                        logger.info(f"Arduino Message: {data}")
                except json.JSONDecodeError:
                    logger.warning(f"Could not decode JSON: {line}")
        except serial.SerialException as e:
            logger.error(f"Serial connection error: {e}. Reconnecting...")
            with serial_lock:
                if ser: ser.close()
                ser = None
            time.sleep(5)
        except Exception as e:
            logger.error(f"An unexpected error occurred: {e}")
            time.sleep(2)


app = Flask(__name__)
CORS(app)


@app.route('/')
def index():
    return render_template('index.html')


@app.route('/data')
def get_data():
    with data_lock:
        return jsonify(latest_data.copy())


@app.route('/control', methods=['POST'])
def control_pump():
    data = request.get_json()
    if not data:
        return jsonify({"error": "No data provided"}), 400

    if 'mode' in data and data['mode'] in ['AUTO', 'MANUAL']:
        command = {"mode": data['mode']}
        if send_command_to_arduino(command):
            with data_lock:
                latest_data['control_mode'] = data['mode']
                if data['mode'] == 'AUTO':
                    latest_data['manual_pump_state'] = False
            logger.info(f"Mode changed to {data['mode']}")
            return jsonify({"success": f"Mode changed to {data['mode']}"})
        else:
            return jsonify({"error": "Failed to send command to Arduino"}), 500

    elif 'pump_manual' in data and data['pump_manual'] in ['ON', 'OFF']:
        with data_lock:
            current_mode = latest_data.get('control_mode')
        
        if current_mode != 'MANUAL':
            return jsonify({"error": "Pump can only be controlled in MANUAL mode"}), 400
        
        command = {"pump_manual": data['pump_manual']}

        if send_command_to_arduino(command):
            with data_lock:
                is_pump_on = (data['pump_manual'] == 'ON')
                latest_data['manual_pump_state'] = is_pump_on
            logger.info(f"Pump manually set to {data['pump_manual']}")
            return jsonify({"success": f"Pump manually set to {data['pump_manual']}"})
        else:
            return jsonify({"error": "Failed to send command to Arduino"}), 500

    return jsonify({"error": "Invalid command"}), 400


@app.route('/history')
def get_history():
    limit = request.args.get('limit', 20, type=int)
    conn = get_db_connection()
    if not conn: return jsonify([]), 500
    try:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT timestamp, water_level_cm, water_level_percent, pump_status, alert_status, control_mode FROM water_level_history ORDER BY timestamp DESC LIMIT %s",
                (limit,))
            history = [{"timestamp": r[0].isoformat(), "water_level_cm": r[1], "water_level_percent": r[2],
                        "pump_status": r[3], "alert_status": r[4], "control_mode": r[5]} for r in cur.fetchall()]
            return jsonify(history)
    except psycopg2.Error as e:
        logger.error(f"Database query error: {e}")
        return jsonify([]), 500
    finally:
        if conn: conn.close()

# --- CODE BARU DITAMBAHKAN DI SINI ---
@app.route('/history', methods=['DELETE'])
def delete_history_data():
    """Menghapus semua data dari tabel riwayat."""
    conn = get_db_connection()
    if not conn:
        return jsonify({"error": "Database connection failed"}), 500
    try:
        with conn.cursor() as cur:
            # TRUNCATE lebih efisien untuk menghapus semua baris dan mereset identity
            cur.execute("TRUNCATE TABLE water_level_history RESTART IDENTITY")
            conn.commit()
            logger.info("All history data has been deleted.")
            return jsonify({"success": "History successfully deleted"})
    except psycopg2.Error as e:
        logger.error(f"Error deleting history data: {e}")
        conn.rollback()  # Rollback jika terjadi error
        return jsonify({"error": "Failed to delete history from database"}), 500
    finally:
        if conn:
            conn.close()
# --- AKHIR DARI KODE BARU ---

@app.route('/status')
def get_status():
    db_conn = get_db_connection()
    status = {
        "database_connected": db_conn is not None,
        "serial_connected": (ser is not None and ser.is_open)
    }
    if db_conn: db_conn.close()
    return jsonify(status)


if __name__ == '__main__':
    try:
        init_database()
    except Exception as e:
        logger.fatal(f"FATAL: Could not initialize database. Exiting. Error: {e}")
        exit(1)

    arduino_thread = threading.Thread(target=read_from_arduino, daemon=True)
    arduino_thread.start()

    app.run(host='0.0.0.0', port=5000)