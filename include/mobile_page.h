#pragma once
#include <Arduino.h>
static const char MOBILE_PAGE[] PROGMEM = R"MOBILE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#102328"><title>April Robot</title>
<style>
:root{color-scheme:dark;--bg:#0b171b;--card:#13282e;--line:#2a4249;--muted:#9eb7bf;--ink:#edf8fa;--accent:#8ce6bc;--red:#ff897e}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px system-ui,-apple-system,sans-serif;padding:22px 18px calc(110px + env(safe-area-inset-bottom))}main{max-width:760px;margin:auto}header{display:flex;justify-content:space-between;align-items:center;margin-bottom:28px}.brand{font-size:18px;font-weight:750;letter-spacing:-.5px}.brand span{color:var(--accent)}.badge{font-size:12px;border:1px solid var(--line);border-radius:30px;padding:7px 10px;color:var(--muted)}.badge.good{color:var(--accent)}h1{font-size:32px;letter-spacing:-1.2px;line-height:1.15;margin:6px 0 10px}p{color:var(--muted);line-height:1.5;margin:8px 0}.eyebrow{font-size:11px;letter-spacing:2px;text-transform:uppercase;color:var(--accent)}button{font:inherit;border:1px solid var(--line);border-radius:14px;padding:14px;color:var(--ink);background:var(--card);cursor:pointer;touch-action:manipulation}button:disabled{opacity:.4;cursor:default}button:focus-visible{outline:3px solid var(--accent);outline-offset:3px}.primary{background:var(--accent);color:#12372b;font-weight:750;border:0;width:100%;margin:16px 0}.layout{display:grid;grid-template-columns:1fr;gap:18px}.card{background:var(--card);border:1px solid var(--line);border-radius:22px;padding:20px}.tabs{display:flex;gap:8px;margin-bottom:18px}.tabs button{flex:1;background:transparent}.tabs button.active{background:#29453e;color:var(--accent);border-color:var(--accent)}.destinations{display:grid;grid-template-columns:1fr 1fr;gap:12px}.destination{text-align:left;min-height:114px;background:#102126}.destination strong{font-size:29px;display:block;margin-bottom:14px}.destination span{color:var(--muted);font-size:13px}.destination.selected{border-color:var(--accent);background:#1b3a32}.destination.seen span{color:var(--accent)}.hint{font-size:13px;margin-top:16px}#manual[hidden],#auto[hidden]{display:none}.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;max-width:330px;margin:12px auto}.pad button{font-size:27px;min-height:72px;background:#19363e;touch-action:none;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none}.pad button.held{background:var(--accent);color:#12372b}.pad .neutral{font-size:13px;color:var(--muted);display:grid;place-items:center}.forward{grid-column:2}.left{grid-column:1}.reverse{grid-column:2}.metrics{display:grid;grid-template-columns:1fr 1fr;gap:20px;margin-top:22px}.metric small{display:block;color:var(--muted);margin-bottom:6px;font-size:12px}.metric b{font-size:19px;font-weight:600}#state{font-size:21px;margin:8px 0}#reason{font-size:14px;min-height:42px}.sensor{font-size:13px;padding:10px;background:#0d2026;border-radius:10px;margin-top:18px}.notice{color:#ffcd91;font-size:13px;margin-top:15px}.dock{position:fixed;bottom:0;left:0;right:0;background:linear-gradient(transparent,var(--bg) 20%);padding:24px 18px calc(14px + env(safe-area-inset-bottom))}.stop{display:block;max-width:760px;width:100%;margin:auto;background:var(--red);color:#36120e;border:0;font-weight:800;letter-spacing:1px;padding:18px}#message{min-height:20px;font-size:13px;color:#ffcd91}.fine{font-size:12px} @media(min-width:680px){body{padding-top:36px}.layout{grid-template-columns:1.2fr 1fr}h1{font-size:40px}.primary{width:auto;min-width:210px;margin-bottom:24px}}
.camera-view{margin:0 0 18px}.camera-top{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:8px}.camera-top h2{font-size:16px;margin:0}.camera-top button{padding:6px 10px;font-size:12px;border-radius:8px}.camera-frame{height:180px;position:relative;background:#071014;border-radius:12px;overflow:hidden}.camera-frame img{width:100%;height:100%;object-fit:contain}.camera-frame img[hidden]{display:none}.camera-frame p{position:absolute;inset:0;display:grid;place-items:center;margin:0;padding:16px;text-align:center;font-size:13px}.camera-frame p[hidden]{display:none}.camera-view figcaption{font-size:12px;color:var(--muted);margin-top:7px}.camera-view .fine{margin:5px 0 0}.pad button{min-height:54px}.pad{gap:8px} @media(min-width:680px){.camera-frame{height:225px}}
</style>
</head>
<body><main>
<header><div class="brand">april<span> / </span>robot</div><span id="connection" class="badge">Connecting…</span></header>
<div class="eyebrow">Your robot, within reach</div><h1>Where to next?</h1>
<p>Choose a tag destination, or take the wheel.</p>
<button id="connect" class="primary">Connect controls</button><div id="message" role="status" aria-live="polite"></div>
<div class="layout">
<section class="card" aria-label="Robot controls">
<div class="tabs"><button id="autoTab" class="active" aria-pressed="true">Destinations</button><button id="manualTab" aria-pressed="false">Manual drive</button></div>
<figure class="camera-view" aria-label="Robot camera">
<div class="camera-top"><h2>Camera</h2><button id="cameraToggle" aria-pressed="true">Pause view</button></div>
<div class="camera-frame"><img id="cameraImage" alt="View from the robot's front camera" hidden><p id="cameraPlaceholder">Connecting to camera…</p></div>
<figcaption id="cameraStatus" role="status">Waiting for a picture</figcaption>
<p id="lensStatus" class="fine">Checking camera calibration…</p>
</figure>
<div id="auto"><div class="destinations">
<button class="destination" data-tag="0" disabled><strong>01 ↗</strong><span>Tag 0 · Not in view</span></button>
<button class="destination" data-tag="1" disabled><strong>02 ↗</strong><span>Tag 1 · Not in view</span></button>
<button class="destination" data-tag="2" disabled><strong>03 ↗</strong><span>Tag 2 · Not in view</span></button>
<button class="destination" data-tag="3" disabled><strong>04 ↗</strong><span>Tag 3 · Not in view</span></button>
</div><p class="hint">Tap a destination to start. If its tag is out of view, the robot will scan for it.</p></div>
<div id="manual" hidden><p class="hint">Hold to move. Release to stop.</p><div class="pad">
<button class="drive forward" data-v="100" data-w="0" aria-label="Drive forward" disabled>↑</button>
<button class="drive left" data-v="0" data-w="100" aria-label="Turn left" disabled>←</button>
<div class="neutral">HOLD</div><button class="drive" data-v="0" data-w="-100" aria-label="Turn right" disabled>→</button>
<button class="drive reverse" data-v="-100" data-w="0" aria-label="Reverse" disabled>↓</button>
</div><p class="notice">Check behind before reversing. The robot has front sensors only.</p></div>
<p class="fine">Keep this page open while driving. Stop disables the controls.</p>
</section>
<section class="card" aria-label="Robot status"><div class="eyebrow">Live status</div><h2 id="state">Waiting for robot</h2><p id="reason">Connect your phone to the AprilRobot Wi-Fi network.</p>
<div class="metrics"><div class="metric"><small>Destination</small><b id="target">—</b></div><div class="metric"><small>Tag distance</small><b id="range">—</b></div><div class="metric"><small>Drive controls</small><b id="armed">—</b></div><div class="metric"><small>Wired link</small><b id="link">—</b></div></div>
<div id="sensors" class="sensor">Front sensors · Waiting</div><p id="calibration" class="fine">65 mm wheels · 20 PPR · Estimated wheel spacing: 158 mm</p>
<button id="maintenance" aria-pressed="false" disabled>Updates &amp; calibration</button>
<p id="maintenanceStatus" class="fine">Stops the motors so you can upload firmware or calibrate the camera.</p>
</section></div>
</main><div class="dock"><button id="stop" class="stop">■ STOP ROBOT</button></div>
<script>

'use strict';
const $ = id => document.getElementById(id);
let token = 0, sequence = 0, status = null, statusAt = 0, busy = 0, held = null, view = 'auto',
    lastPulse = 0, polling = false;
const message = text => { $('message').textContent = text; };

const cameraUrl = 'http://192.168.4.250';
let cameraEnabled = true, cameraBusy = false, cameraController = null, cameraObjectUrl = null;
let cameraSeenAt = 0, cameraAge = 0, cameraGeneration = 0, lensBusy = false;

function hideCamera(text) {
    $('cameraImage').hidden = true;
    $('cameraPlaceholder').hidden = false;
    $('cameraPlaceholder').textContent = text;
    cameraSeenAt = 0;
    if (cameraObjectUrl) {
        URL.revokeObjectURL(cameraObjectUrl);
        cameraObjectUrl = null;
    }
    $('cameraImage').removeAttribute('src');
}

async function cameraPicture() {
    if (!cameraEnabled || cameraBusy || document.hidden)
        return;
    cameraBusy = true;
    const generation = cameraGeneration;
    const controller = new AbortController();
    cameraController = controller;
    const timer = setTimeout(() => controller.abort(), 1800);
    try {
        const started = performance.now();
        const response = await fetch(cameraUrl + '/frame.jpg', {cache: 'no-store', signal: controller.signal});
        if (!response.ok)
            throw Error('Camera unavailable');
        const ageHeader = response.headers.get('X-Frame-Age-Ms');
        const blob = await response.blob();
        const age = Number(ageHeader) + performance.now() - started;
        if (ageHeader === null || !Number.isFinite(age) || age < 0 || age > 2000 || blob.type !== 'image/jpeg')
            throw Error('Camera picture is stale');
        if (generation !== cameraGeneration || document.hidden)
            return;
        const objectUrl = URL.createObjectURL(blob);
        const probe = new Image();
        probe.src = objectUrl;
        try {
            await probe.decode();
        } catch (error) {
            URL.revokeObjectURL(objectUrl);
            throw error;
        }
        if (generation !== cameraGeneration || document.hidden) {
            URL.revokeObjectURL(objectUrl);
            return;
        }
        const previous = cameraObjectUrl;
        cameraObjectUrl = objectUrl;
        $('cameraImage').src = objectUrl;
        $('cameraImage').hidden = false;
        $('cameraPlaceholder').hidden = true;
        if (previous)
            URL.revokeObjectURL(previous);
        cameraSeenAt = performance.now();
        cameraAge = age;
        $('cameraStatus').textContent = 'Grayscale view · updates up to twice a second';
    } catch (error) {
        if (generation === cameraGeneration) {
            hideCamera('Camera unavailable — retrying…');
            $('cameraStatus').textContent = 'No live picture. Check the robot before moving.';
        }
    } finally {
        clearTimeout(timer);
        cameraController = null;
        cameraBusy = false;
    }
}

async function lensStatus() {
    if (lensBusy || document.hidden)
        return;
    lensBusy = true;
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 1800);
    try {
        const response = await fetch(cameraUrl + '/calibration', {cache: 'no-store', signal: controller.signal});
        if (!response.ok)
            throw Error('Unavailable');
        const data = await response.json();
        $('lensStatus').textContent = data.restart_required ? 'Calibration saved — restart camera to apply'
            : data.calibrated ? 'Saved lens calibration in use'
            : 'Lens not calibrated yet — using starting estimates';
    } catch (error) {
        $('lensStatus').textContent = 'Camera calibration status unavailable';
    } finally {
        clearTimeout(timer);
        lensBusy = false;
    }
}

$('cameraToggle').addEventListener('click', () => {
    cameraEnabled = !cameraEnabled;
    ++cameraGeneration;
    if (cameraController)
        cameraController.abort();
    $('cameraToggle').textContent = cameraEnabled ? 'Pause view' : 'Resume view';
    $('cameraToggle').setAttribute('aria-pressed', String(cameraEnabled));
    hideCamera(cameraEnabled ? 'Connecting to camera…' : 'Camera view paused');
    $('cameraStatus').textContent = cameraEnabled ? 'Waiting for a picture' : 'View paused';
});

function clearHeld() {
    if (held)
        held.classList.remove('held');
    held = null;
}

function resetControls(text) {
    token = 0;
    clearHeld();
    message(text);
    render();
}

function emergencyStop(text = 'Stopped. Connect controls to continue.') {
    clearHeld();
    token = 0;
    render();
    message(text);
    if (navigator.sendBeacon)
        navigator.sendBeacon('/api/stop');
    else
        fetch('/api/stop', {method : 'POST', keepalive : true}).catch(() => {});
}

async function json(url, options = {}) {
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 450);
    try {
        const r = await fetch(url, {cache : 'no-store', ...options, signal : controller.signal});
        const data = await r.json();
        if (!r.ok)
            throw Error(data.error || 'Request rejected');
        return data;
    } finally {
        clearTimeout(timer);
    }
}

function deadline() {
    if (!status || performance.now() - statusAt > 400)
        throw Error('Robot status is stale');
    return (status.now + 600) >>> 0;
}

async function action(name, args = {}, force = false) {
    if (!token || (busy && !force))
        return false;
    const owner = token, issuedSequence = ++sequence;
    busy++;
    try {
        const params = new URLSearchParams({
            token : String(owner),
            seq : String(issuedSequence),
            deadline : String(deadline()),
            action : name,
            ...args
        });
        await json('/api/control?' + params, {method : 'POST'});
        return token === owner;
    } catch (e) {
        if (token === owner && issuedSequence === sequence)
            emergencyStop('Connection interrupted. Robot stopped; reconnect controls.');
        return false;
    } finally {
        busy--;
    }
}

function render() {
    const fresh = status && performance.now() - statusAt < 700;
    $('connection').textContent = fresh ? 'Robot connected' : 'Connection lost';
    $('connection').classList.toggle('good', !!fresh);
    $('connect').disabled = !!token || !fresh || !status.canConnect;
    $('connect').textContent = token ? 'Controls connected' : 'Connect controls';
    const controlsReady = fresh && status.enabled && !status.maintenance && status.state !== 'ARMING';
    document.querySelectorAll('.destination').forEach(b => { b.disabled = !token || !controlsReady; });
    document.querySelectorAll('.drive').forEach(
        b => { b.disabled = !token || !controlsReady || status.mode !== 'MANUAL'; });
    $('maintenance').disabled = !fresh || status.updateBusy;
    if (!status)
        return;
    $('state').textContent = status.state.replaceAll('_', ' ');
    $('reason').textContent = status.reason;
    $('target').textContent = status.mode === 'AUTO'     ? 'Tag ' + status.target
                              : status.mode === 'MANUAL' ? 'Manual'
                                                         : 'None';
    $('range').textContent = status.range === null ? '—' : status.range.toFixed(2) + ' m';
    $('armed').textContent = status.state === 'ARMING' ? 'Starting…' : status.enabled ? 'Enabled' : 'Disabled';
    $('maintenance').textContent = status.maintenance ? 'Finish maintenance' : 'Updates & calibration';
    $('maintenance').setAttribute('aria-pressed', String(status.maintenance));
    $('maintenanceStatus').textContent = status.updateBusy ? 'Writing firmware or calibration. Motors disabled.'
        : status.maintenance ? (status.cameraMaintenance ? 'Both boards ready. Motors disabled.' : 'Motors disabled. Waiting for camera…')
        : 'Stops the motors so you can upload firmware or calibrate the camera.';
    $('link').textContent = status.link ? 'Connected' : 'Lost';
    $('sensors').textContent = status.link ? 'Front left: ' + (status.ir[0] ? 'blocked' : 'clear') +
                                                 ' · Right: ' + (status.ir[1] ? 'blocked' : 'clear')
                                           : 'Front sensors · Link lost';
    document.querySelectorAll('.destination').forEach(b => {
        const id = Number(b.dataset.tag), seen = (status.visible & (1 << id)) !== 0;
        b.classList.toggle('selected', status.mode === 'AUTO' && status.target === id);
        b.classList.toggle('seen', seen);
        b.querySelector('span').textContent =
            'Tag ' + id + ' · ' + (seen ? 'In view' : 'Not in view');
    });
}

async function poll() {
    if (polling || document.hidden)
        return;
    polling = true;
    try {
        status = await json('/api/state');
        statusAt = performance.now();
        if (token && !status.owned)
            resetControls('Control session ended. Connect controls to continue.');
        render();
    } catch (e) {
        if (token)
            emergencyStop('Connection lost. Robot stopped.');
        statusAt = 0;
        render();
    } finally {
        polling = false;
    }
}

$('connect').addEventListener('click', async () => {
    try {
        const data = await json('/api/claim?deadline=' + deadline(), {method : 'POST'});
        token = data.token;
        sequence = 0;
        message('Controls enabled. Wait two seconds, then choose a destination or hold an arrow.');
        render();
        if (view === 'manual')
            await action('manual');
    } catch (e) {
        message(e.message);
    }
});
$('maintenance').addEventListener('click', async () => {
    clearHeld();
    token = 0;
    render();
    try {
        const enabled = status.maintenance ? '0' : '1';
        await json('/api/maintenance?enabled=' + enabled + '&deadline=' + deadline(), {method: 'POST'});
        message(enabled === '1' ? 'Motors disabled for updates and calibration.' : 'Finished. Connect controls when ready.');
        await poll();
    } catch (error) {
        emergencyStop('Maintenance request failed. Robot stopped; try again.');
    }
});
document.querySelectorAll('.destination').forEach(b => b.addEventListener('click', async () => {
    clearHeld();
    if (await action('auto', {tag : b.dataset.tag}, true))
        message('Navigating to tag ' + b.dataset.tag + '.');
}));

function tab(which) {
    clearHeld();
    view = which;
    $('auto').hidden = which !== 'auto';
    $('manual').hidden = which !== 'manual';
    message(which === 'manual' ? 'Manual mode. Hold an arrow to move.'
                               : 'Choose a destination to begin.');
    $('autoTab').classList.toggle('active', which === 'auto');
    $('manualTab').classList.toggle('active', which === 'manual');
    $('autoTab').setAttribute('aria-pressed', String(which === 'auto'));
    $('manualTab').setAttribute('aria-pressed', String(which === 'manual'));
    render();
    if (token)
        action(which === 'manual' ? 'manual' : 'idle', {}, true);
    if (which === 'manual' && window.innerWidth < 680)
        document.querySelector('[aria-label="Robot controls"]').scrollIntoView({block: 'start'});
}

$('autoTab').addEventListener('click', () => tab('auto'));
$('manualTab').addEventListener('click', () => tab('manual'));

function release() {
    if (!held)
        return;
    clearHeld();
    action('drive', {v : '0', w : '0'}, true);
}

document.querySelectorAll('.drive').forEach(b => {
    b.addEventListener('pointerdown', e => {
        e.preventDefault();
        if (!token || held || b.disabled)
            return;
        b.setPointerCapture(e.pointerId);
        held = b;
        b.classList.add('held');
        action('drive', {v : b.dataset.v, w : b.dataset.w}, true);
        lastPulse = performance.now();
    });
    ['pointerup', 'pointercancel', 'lostpointercapture'].forEach(
        event => b.addEventListener(event, release));
    b.addEventListener('contextmenu', e => e.preventDefault());
    b.addEventListener('keydown', e => {
        if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) {
            e.preventDefault();
            if (!token || held)
                return;
            held = b;
            b.classList.add('held');
            action('drive', {v : b.dataset.v, w : b.dataset.w}, true);
        }
    });
    b.addEventListener('keyup', e => {
        if (e.key === ' ' || e.key === 'Enter') {
            e.preventDefault();
            release();
        }
    });
});
$('stop').addEventListener('click', () => emergencyStop());
document.addEventListener('visibilitychange', () => {
    ++cameraGeneration;
    if (cameraController)
        cameraController.abort();
    hideCamera(cameraEnabled ? 'Waiting for a new picture…' : 'Camera view paused');
    if (document.hidden && token)
        emergencyStop('Stopped while page was hidden.');
});
window.addEventListener('pagehide', () => {
    if (token)
        emergencyStop();
});
window.addEventListener('blur', () => {
    if (token)
        emergencyStop('Stopped when controls lost focus.');
});
setInterval(() => {
    if (!token || document.hidden)
        return;
    const now = performance.now();
    if (now - lastPulse < 100)
        return;
    lastPulse = now;
    if (held)
        action('drive', {v : held.dataset.v, w : held.dataset.w});
    else
        action('heartbeat');
}, 50);
setInterval(poll, 200);
setInterval(cameraPicture, 500);
setInterval(lensStatus, 5000);
setInterval(() => {
    if (cameraSeenAt && performance.now() - cameraSeenAt + cameraAge > 2000) {
        hideCamera('Picture is stale — waiting for camera…');
        $('cameraStatus').textContent = 'No live picture. Check the robot before moving.';
    }
}, 200);
poll();
cameraPicture();
lensStatus();
</script></body></html>
)MOBILE";
