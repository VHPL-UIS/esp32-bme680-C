from flask import Flask, request, jsonify
import sqlite3
from datetime import datetime
import os

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

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)