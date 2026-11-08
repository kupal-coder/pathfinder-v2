#!/usr/bin/env python3
"""Static server + closed-loop runner for the Pathfinder demo.

The demo page never reimplemented the solver in JavaScript. `/api/run` executes
the real C++ closed-loop binary (tools/sim_server.cpp), which drives the same
AutoPlayer/simulator the mod uses, and returns the frame trace as JSON.
"""
import argparse
import json
import os
import subprocess
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT = os.path.dirname(os.path.abspath(__file__))
BINARY = os.path.join(ROOT, "pathfinder_sim")


def build_binary():
    """Compiles tools/sim_server.cpp if needed (or if the sources changed)."""
    src_root = os.path.abspath(os.path.join(ROOT, ".."))
    sources = [
        "tools/sim_server.cpp",
        "src/solver/Types.cpp",
        "src/solver/Physics.cpp",
        "src/solver/Planner.cpp",
        "src/bot/AutoPlayer.cpp",
        "src/bot/Practice.cpp",
    ]
    newest = 0
    for rel in sources:
        path = os.path.join(src_root, rel)
        if os.path.exists(path):
            newest = max(newest, os.path.getmtime(path))
    if os.path.exists(BINARY) and os.path.getmtime(BINARY) > newest:
        return

    print("building closed-loop runner...", flush=True)
    cmd = [
        "g++", "-std=c++17", "-O2",
        f"-I{src_root}/src", f"-I{src_root}/tests",
        "-o", BINARY,
    ] + [os.path.join(src_root, s) for s in sources]
    subprocess.run(cmd, check=True)
    print("built", BINARY, flush=True)


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def log_message(self, fmt, *args):  # keep the console readable
        pass

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/run":
            self.handle_run(parse_qs(parsed.query))
            return
        if parsed.path == "/api/health":
            self.respond(200, {"ok": True})
            return
        return super().do_GET()

    def handle_run(self, query):
        try:
            seed = int(query.get("seed", ["1"])[0])
            difficulty = max(1, min(5, int(query.get("difficulty", ["2"])[0])))
            horizon = max(20, min(600, int(query.get("horizon", ["240"])[0])))
            min_frames = max(10, min(600, int(query.get("minFrames", ["120"])[0])))
        except ValueError:
            self.respond(400, {"error": "seed, difficulty, horizon and minFrames must be integers"})
            return

        seed = max(1, min(1_000_000, seed))
        try:
            out = subprocess.run(
                [BINARY, "--seed", str(seed), "--difficulty", str(difficulty),
                 "--horizon", str(horizon), "--min-frames", str(min_frames)],
                capture_output=True, text=True, timeout=180, check=True,
            ).stdout
        except subprocess.CalledProcessError as exc:
            self.respond(500, {"error": "runner failed", "detail": exc.stderr[-800:]})
            return
        except subprocess.TimeoutExpired:
            self.respond(504, {"error": "runner timed out"})
            return

        try:
            payload = json.loads(out)
        except json.JSONDecodeError:
            self.respond(500, {"error": "runner produced invalid JSON"})
            return
        self.respond(200, payload)

    def respond(self, status, payload):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=int(os.environ.get("PORT", "8080")))
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()

    if not args.no_build:
        build_binary()
    if not os.path.exists(BINARY):
        sys.exit("missing runner binary; run without --no-build")

    server = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print(f"Pathfinder demo on http://0.0.0.0:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
