import json
from flask import Flask, make_response, request, jsonify, send_from_directory
from influxdb_client import InfluxDBClient, Point, WriteOptions
from influx_token import INFLUX_TOKEN
import sqlite3
from datetime import datetime
import os

INFLUX_URL = "http://localhost:8086"
TOKEN = INFLUX_TOKEN
INFLUX_ORG = "vhpl"
INFLUX_BUCKET = "sensor"

influx_client = InfluxDBClient(
    url=INFLUX_URL,
    token=TOKEN,
    org=INFLUX_ORG)
write_api = influx_client.write_api(write_options=WriteOptions(batch_size=1))

app = Flask(__name__)
DB_FILE = 'sensor_data.db'

def init_db():
    if not os.path.exists(DB_FILE):
        conn = sqlite3.connect(DB_FILE)
        c = conn.cursor()
        c.execute('''
            CREATE TABLE sensor_data (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                temperature REAL,
                humidity REAL,
                pressure REAL,
                gas_resistance REAL,
                timestamp TEXT
            )
        ''')
        conn.commit()
        conn.close()
        print(f"Database {DB_FILE} created and initialized.")

init_db()

@app.route('/sensor', methods=['POST'])
def sensor_data():
    try:
        data = request.get_json()
        if not data or 'temperature' not in data:
            return jsonify({'error': 'Invalid data'}), 400
        
        temperature = data['temperature']
        humidity = data['humidity']
        pressure = data['pressure']
        gas_resistance = data['gas_resistance']
        timestamp = datetime.now().isoformat()

        conn = sqlite3.connect(DB_FILE)
        c = conn.cursor()
        c.execute('INSERT INTO sensor_data (temperature, humidity, pressure, gas_resistance, timestamp) VALUES (?, ?, ?, ?, ?)', (temperature, humidity, pressure, gas_resistance, timestamp))
        conn.commit()
        conn.close()

        print(f"Inserted data: {data} at {timestamp}")

        # Write to InfluxDB
        point = (
            Point("sensor")
            .field("temperature", temperature)
            .field("humidity", humidity)
            .field("pressure", pressure)
            .field("gas_resistance", gas_resistance)
        )
        write_api.write(bucket=INFLUX_BUCKET, record=point)
        print(f"Data written to InfluxDB: {data}")

        return jsonify({'status': 'ok'}), 200
    except Exception as e:
        print("Failed to parse JSON:", e)
        return jsonify({"status": "error", "message": str(e)}), 400

@app.route('/latest', methods=['GET'])
def latest():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT * FROM sensor_data ORDER BY id DESC LIMIT 1')
    row = c.fetchone()
    conn.close()
    if row:
        return jsonify({'row_id': row[0], 'temperature': row[1], 'humidity': row[2], 'pressure': row[3], 'gas_resistance': row[4], 'timestamp': row[5]})
    else:
        return jsonify({'error': 'No data found'}), 404
    
@app.route('/firmware/latest', methods=['GET'])
def firmware_latest():
    firmware_dir = os.path.join(os.path.dirname(__file__), 'firmware')
    filename = 'firmware.bin'
    return send_from_directory(firmware_dir, filename, as_attachment=True)

@app.route('/firmware/version', methods=['GET'])
def firmware_version():
    firmware_dir = os.path.join(os.path.dirname(__file__), 'firmware')
    version_file = os.path.join(firmware_dir, 'version.json')
    if os.path.exists(version_file):
        with open(version_file, 'r') as f:
            version_data = json.load(f)
        resp = make_response(jsonify(version_data))
        resp.headers['Content-Encoding'] = 'identity'
        resp.headers['Transfer-Encoding'] = 'identity'
        return resp
    else:
        return jsonify({'error': 'Version file not found'}), 404

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, ssl_context=('certs/cert.pem', 'certs/key.pem'))