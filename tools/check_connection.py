"""Read robot status repeatedly to diagnose HTTP delays without sending commands."""
import argparse
import json
import time
import urllib.request
import urllib.error
from collections import Counter


def camera_status(opener):
    """On an image failure, distinguish the image handler from the entire server."""
    try:
        with opener.open('http://192.168.4.250/calibration', timeout=1.5) as response:
            state = json.load(response)
        keys = ('bootId', 'frameId', 'frameAgeMs', 'captureStatus', 'captureError',
                'wifiRssi', 'freeHeap', 'freePsram', 'busy')
        print('  Camera status responds: ' + ' | '.join(f'{key}={state.get(key)}' for key in keys), flush=True)
    except (OSError, ValueError) as error:
        print(f'  Camera status also failed: {error}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--camera', action='store_true', help='check camera JPEG delivery instead of robot status')
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error('--seconds must be positive')
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    counts = Counter()
    latencies = []
    previous_boot = None
    previous_uptime = None
    until = time.monotonic() + args.seconds
    print('Reading camera pictures only; leave motors idle. Ctrl+C stops the check.' if args.camera
          else 'Reading status only; leave motors idle. Ctrl+C stops the check.', flush=True)
    try:
        while time.monotonic() < until:
            start = time.monotonic()
            try:
                if args.camera:
                    request = urllib.request.Request('http://192.168.4.250/frame.jpg',
                                                     headers={'Cache-Control': 'no-cache'})
                    with opener.open(request, timeout=1.8) as response:
                        frame = response.read()
                        age = response.headers.get('X-Frame-Age-Ms')
                        session = response.headers.get('X-Camera-Session')
                        content_type = response.headers.get_content_type()
                    elapsed = (time.monotonic() - start) * 1000
                    if content_type != 'image/jpeg' or not frame.startswith(b'\xff\xd8'):
                        raise ValueError('Response is not a JPEG image')
                    counts['ok'] += 1
                    latencies.append(elapsed)
                    if elapsed >= 450:
                        counts['slow'] += 1
                    if previous_boot is not None and session != previous_boot:
                        counts['restarts'] += 1
                    previous_boot = session
                    print(f'{elapsed:6.0f} ms | JPEG bytes={len(frame)} | age_ms={age} '
                          f'| camera_session={session}', flush=True)
                    continue
                request = urllib.request.Request('http://192.168.4.1/api/state',
                                                 headers={'Cache-Control': 'no-cache'})
                with opener.open(request, timeout=1.5) as response:
                    state = json.load(response)
                elapsed = (time.monotonic() - start) * 1000
                latencies.append(elapsed)
                counts['ok'] += 1
                if elapsed >= 450:
                    counts['slow'] += 1
                boot = state.get('bootId')
                uptime = state.get('now')
                if (boot is not None and boot == previous_boot and
                        isinstance(uptime, int) and isinstance(previous_uptime, int) and
                        uptime < previous_uptime and previous_uptime - uptime < 0x80000000):
                    counts['stale'] += 1
                if previous_boot is not None and boot is not None and boot != previous_boot:
                    counts['restarts'] += 1
                if boot is not None:
                    previous_boot = boot
                previous_uptime = uptime
                print(f'{elapsed:6.0f} ms | boot={boot} | uptime_ms={state.get("now")} '
                      f'| wired_link={state.get("link")} | state={state.get("state")} '
                      f'| pwm={state.get("pwm")} | ticks={state.get("ticks")} '
                      f'| frame_age_ms={state.get("age")} | reason={state.get("reason")}', flush=True)
                if state.get('stopReason') is not None:
                    print(f'  stop_reason={state["stopReason"]} | command_age_ms={state.get("commandAgeMs")}', flush=True)
            except urllib.error.HTTPError as error:
                counts['failed'] += 1
                print(f'HTTP {error.code}: {error.read(1024).decode("utf-8", errors="replace")}', flush=True)
                error.close()
                if args.camera:
                    camera_status(opener)
            except (OSError, ValueError) as error:
                counts['failed'] += 1
                print(f'FAILED: {error}', flush=True)
                if args.camera:
                    camera_status(opener)
            finally:
                time.sleep(max(0, (2 if args.camera else .25) - (time.monotonic() - start)))
    except KeyboardInterrupt:
        pass
    print(f'\nSuccessful: {counts["ok"]}; failed: {counts["failed"]}; '
          f'responses >=450 ms: {counts["slow"]}; boot ID changes: {counts["restarts"]}; '
          f'backward uptime responses: {counts["stale"]}')
    if latencies:
        print(f'HTTP average: {sum(latencies) / len(latencies):.0f} ms; '
              f'maximum: {max(latencies):.0f} ms')


if __name__ == '__main__':
    main()
