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
                timestamp TEXT
            )
        ''')
        conn.commit()
        conn.close()
        print(f"Database {DB_FILE} created and initialized.")

init_db()

@app.route('/sensor', methods=['POST'])
def sensor_data():
    data = request.get_json()
    if not data or 'temperature' not in data:
        return jsonify({'error': 'Invalid data'}), 400
    
    temperature = data['temperature']
    timestamp = datetime.now().isoformat()

    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('INSERT INTO sensor_data (temperature, timestamp) VALUES (?, ?)', (temperature, timestamp))
    conn.commit()
    conn.close()

    print(f"Inserted data: {temperature} at {timestamp}")
    return jsonify({'status': 'ok'}), 200

@app.route('/latest', methods=['GET'])
def latest():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('SELECT * FROM sensor_data ORDER BY id DESC LIMIT 1')
    row = c.fetchone()
    conn.close()
    if row:
        return jsonify({'row_id': row[0], 'temperature': row[1], 'timestamp': row[2]})
    else:
        return jsonify({'error': 'No data found'}), 404

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)