"""Install the template's freely licensed fonts for the current Windows user."""
from concurrent.futures import ThreadPoolExecutor
import ctypes
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request
import winreg

PROJECT = Path(__file__).resolve().parents[3]
DEST = PROJECT / 'ArtSource/UI/HUDuiPRO/Fonts'
URLS = {
    **{f'Orbitron-{weight}.ttf': f'https://raw.githubusercontent.com/theleagueof/orbitron/master/Orbitron%20{weight}.ttf' for weight in ('Light', 'Medium', 'Bold')},
    'Orbitron-OFL.md': 'https://raw.githubusercontent.com/theleagueof/orbitron/master/Open%20Font%20License.markdown',
    **{f'Rajdhani-{weight}.ttf': f'https://raw.githubusercontent.com/google/fonts/main/ofl/rajdhani/Rajdhani-{weight}.ttf' for weight in ('Light', 'Regular', 'SemiBold', 'Bold')},
    'Rajdhani-OFL.txt': 'https://raw.githubusercontent.com/google/fonts/main/ofl/rajdhani/OFL.txt',
}

def download(pair):
    name, url = pair
    path = DEST / name
    request = urllib.request.Request(url, headers={'User-Agent': 'GuLiStrike-HUDuiPRO-Import'})
    data = urllib.request.urlopen(request, timeout=40).read()
    if name.endswith('.ttf') and data[:4] not in (b'\x00\x01\x00\x00', b'OTTO'):
        raise ValueError(f'Not a font: {name}')
    path.write_bytes(data)
    return {'file': name, 'url': url, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}

def main():
    DEST.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=6) as pool:
        manifest = list(pool.map(download, URLS.items()))
    user_fonts = Path.home() / 'AppData/Local/Microsoft/Windows/Fonts'
    user_fonts.mkdir(parents=True, exist_ok=True)
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r'Software\Microsoft\Windows NT\CurrentVersion\Fonts') as key:
        for entry in manifest:
            if not entry['file'].endswith('.ttf'):
                continue
            target = user_fonts / entry['file']
            shutil.copy2(DEST / entry['file'], target)
            winreg.SetValueEx(key, Path(entry['file']).stem + ' (TrueType)', 0, winreg.REG_SZ, str(target))
            entry['registeredFaces'] = ctypes.windll.gdi32.AddFontResourceExW(str(target), 0, None)
            entry['installedPath'] = str(target)
    result = ctypes.c_size_t()
    ctypes.windll.user32.SendMessageTimeoutW(0xffff, 0x001d, 0, 0, 2, 1000, ctypes.byref(result))
    (DEST / 'sources.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(manifest, ensure_ascii=False, indent=2))

if __name__ == '__main__':
    main()
