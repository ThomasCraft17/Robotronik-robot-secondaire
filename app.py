"""Pi 5 web controller: browser (ZQSD or gamepad) -> WebSocket -> your Python code.

Run:  pip install -r requirements.txt && python app.py
Open: http://<ip-of-the-pi>:5000
"""
import json
import threading
import time

from flask import Flask, send_from_directory
from flask_sock import Sock

app = Flask(__name__, static_folder="static")
sock = Sock(app)

# Latest command received from the browser.
# x, y in [-1, 1] (x: left/right, y: forward/back), l and r are booleans.
state = {"x": 0.0, "y": 0.0, "l": False, "r": False}
last_msg = 0.0
lock = threading.Lock()


def mix(x, y):
    """Tank/differential drive mixing: speed y and steering x -> left/right motor values (-1..1)."""
    left, right = y + x, y - x
    biggest = max(1.0, abs(left), abs(right))
    return left / biggest, right / biggest


def apply_output(x, y, l, r):
    """>>> PUT YOUR ROBOT CODE HERE <<<
    Called ~50 times per second.
      y: speed, -1..1 (keyboard: 5 steps of 0.2, gamepad: analog)
      x: steering, -1..1
      l, r: L / R buttons (booleans)
    Example with gpiozero once you know the wiring:
        from gpiozero import Robot
        robot = Robot(left=(4, 14), right=(17, 18))
        robot.value = mix(x, y)
    """
    left, right = mix(x, y)
    print(f"left={left:+.2f} right={right:+.2f} L={int(l)} R={int(r)}   ", end="\r")


def control_loop():
    """Runs forever; your existing Python logic can live here too."""
    while True:
        with lock:
            s = dict(state)
            stale = time.time() - last_msg > 0.5
        if stale:  # failsafe: browser closed / wifi lost -> stop everything
            s = {"x": 0.0, "y": 0.0, "l": False, "r": False}
        apply_output(s["x"], s["y"], s["l"], s["r"])
        time.sleep(0.02)


@app.route("/")
def index():
    return send_from_directory("static", "index.html")


@sock.route("/ws")
def ws(conn):
    global last_msg
    while True:
        data = conn.receive()
        if data is None:
            break
        try:
            m = json.loads(data)
            with lock:
                state["x"] = max(-1.0, min(1.0, float(m["x"])))
                state["y"] = max(-1.0, min(1.0, float(m["y"])))
                state["l"] = bool(m["l"])
                state["r"] = bool(m["r"])
                last_msg = time.time()
        except (ValueError, KeyError, TypeError):
            pass
    with lock:  # connection closed -> stop
        state.update(x=0.0, y=0.0, l=False, r=False)


if __name__ == "__main__":
    threading.Thread(target=control_loop, daemon=True).start()
    app.run(host="0.0.0.0", port=5000, threaded=True)
