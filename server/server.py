from flask import Flask, request

app = Flask(__name__)

@app.route('/sensor', methods=['POST'])
def hello():
    data = request.data.decode('utf-8')
    print(f"Received sensor data: {data}")
    return 'OK', 200

if __name__ == '__main__':
    app.run(host='0.0.0', port=5000)