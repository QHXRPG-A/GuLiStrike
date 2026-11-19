"""Render the saved B17 models at the same framing and light for both factions."""

import hashlib
import json
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Euler, Vector


sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bake_surface_marks import B13, BASELINE, DEST, REFERENCES, REST, ROOT, STYLE_PROPERTY, read_json
from bake_surface_detail import linear_color
from build_reference_ink import CAMERAS, REFERENCE_SIZE


ANGLES = ('Hero', 'Front', 'Left', 'Back')
ANGLE_SIZE = (900, 700)
DETAIL_SIZE = (1100, 800)
# A5 source crop and the corresponding B17 model area in the 850×630 camera.
DETAILS = {
    'AirBase': ('近端导轨横纹', (644, 477, 777, 644), (535, 370, 609, 466)),
    'CloningCenter': ('右上培养舱窗与锁扣', (481, 180, 594, 286), (441, 185, 498, 247)),
    'CommandCenter': ('前部设备罩检修板', (458, 514, 647, 733), (433, 386, 510, 505)),
    'MilitaryFactory': ('左侧装甲分板纹', (143, 443, 396, 688), (271, 350, 400, 475)),
    'Reactor': ('顶盖锁柄', (354, 93, 547, 269), (399, 181, 467, 258)),
    'StrategyCenter': ('回转座刻度和定位牌', (515, 473, 641, 585), (433, 382, 504, 432)),
}
TEAM_AREAS = {
    'AirBase': '穹顶机构罩、塔台护板与下缘环带',
    'CloningCenter': '顶罩边环和两侧设备箱',
    'CommandCenter': '前部设备罩与平台顶板',
    'MilitaryFactory': '屋顶护板、门楣与底裙',
    'Reactor': '壳体外护板与顶环',
    'StrategyCenter': '腰部设备箱与阵列背箱',
}
FIXED_SWATCHES = {
    'AirBase': [('穹顶和塔台压条', '#FEE4D9'), ('机库门', '#D5C09C'), ('雷达', '#2C3735'), ('定位标记', '#EE9D58')],
    'CloningCenter': [('塔身装甲', '#FEE4D9'), ('支撑板', '#D5C09C'), ('舱窗', '#0D9099'), ('软管与背架', '#2C3735')],
    'CommandCenter': [('塔冠', '#D5C09C'), ('观察环缘', '#FEE4D9'), ('窗带', '#1A182F'), ('状态灯', '#0D9099')],
    'MilitaryFactory': [('墙板', '#557B78'), ('卷门', '#D5C09C'), ('弯管', '#2C3735'), ('护角', '#EE9D58')],
    'Reactor': [('环肩和腿甲', '#FEE4D9'), ('散热片', '#557B78'), ('锁柄', '#EE9D58'), ('散热唇', '#0D9099')],
    'StrategyCenter': [('阵列边框', '#FEE4D9'), ('阵列工作面', '#2C3735'), ('转台刻度', '#0A6F73'), ('定位牌', '#EE9D58')],
}


def world_vertices(obj, depsgraph):
    evaluated = obj.evaluated_get(depsgraph)
    return np.asarray([evaluated.matrix_world @ v.co for v in evaluated.data.vertices], dtype=np.float64)


def render(scene, path, size, rotation, center, width):
    scene.camera.location = center
    scene.camera.rotation_euler = rotation
    scene.camera.data.ortho_scale = width
    scene.render.resolution_x, scene.render.resolution_y = size
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    with path.open('rb') as handle:
        header = handle.read(24)
    assert header[:8] == b'\x89PNG\r\n\x1a\n'
    assert (int.from_bytes(header[16:20], 'big'), int.from_bytes(header[20:24], 'big')) == size


def angle_views(rest, cameras):
    views = {}
    required_widths = []
    for angle in ANGLES:
        rotation = Euler(cameras[angle][1])
        matrix = np.asarray(rotation.to_matrix())
        coordinates = rest @ matrix
        low, high = coordinates.min(axis=0), coordinates.max(axis=0)
        span = high - low
        required_widths.append(max(span[0], span[1] * ANGLE_SIZE[0] / ANGLE_SIZE[1]))
        center = (low + high) / 2
        center[2] = high[2] + 30
        views[angle] = (rotation, Vector(center @ matrix.T))
    return views, max(required_widths) * 1.14


def detail_view(camera, crop):
    location, rotation, width = camera
    angle = Euler(rotation)
    basis = angle.to_matrix()
    x0, y0, x1, y1 = crop
    reference_w, reference_h = REFERENCE_SIZE
    pixel = width / reference_w
    offset = Vector(((x0 + x1 - reference_w) * .5 * pixel,
                     (reference_h - y0 - y1) * .5 * pixel, 0))
    detail_width = max(x1 - x0, (y1 - y0) * DETAIL_SIZE[0] / DETAIL_SIZE[1]) * pixel
    return angle, Vector(location) + basis @ offset, detail_width


def material_swatch_present(materials, hex_color):
    color = linear_color(hex_color)
    for material in materials:
        if material.get('Base sRGB') == hex_color:
            return True
        for node in material.node_tree.nodes:
            sockets = list(node.inputs) + (list(node.outputs) if node.type == 'RGB' else [])
            if any(socket.type == 'RGBA' and np.allclose(socket.default_value, color, atol=1e-6) for socket in sockets):
                return True
    return False


def build(output, project=DEST, focus=None):
    assert Path(bpy.data.filepath) == project
    source_sha = hashlib.sha256(project.read_bytes()).hexdigest()
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    edges = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    refs = {row['key']: row for row in read_json(REFERENCES / 'Reference_A5_Manifest.json')['deliverables']}
    cameras = read_json(CAMERAS)['paired_cameras']
    output.mkdir(parents=True, exist_ok=True)
    entries = []
    for key, row in source.items():
        scene = bpy.data.scenes[row['scene']]
        bpy.context.window.scene = scene
        bpy.context.view_layer.update()
        assert scene[STYLE_PROPERTY] == 1 and scene.camera.data.type == 'ORTHO'
        blue, red = (bpy.data.objects[name] for name in row['objects'])
        assert blue.data == red.data
        depsgraph = bpy.context.evaluated_depsgraph_get()
        rest = np.asarray([attr.vector.copy() for attr in blue.data.attributes[REST].data], dtype=np.float64)
        offsets = [world_vertices(obj, depsgraph) - rest for obj in (blue, red)]
        shifts = [offset.mean(axis=0) for offset in offsets]
        mismatch = np.linalg.norm((offsets[0] - shifts[0]) - (offsets[1] - shifts[1]), axis=1).max()
        view_cameras = {angle: cameras[f'{key}_{angle}'] for angle in ANGLES}
        assert view_cameras['Hero'] == edges[key]['reference_camera']
        views, width = angle_views(rest, view_cameras)
        alignment_px = float(mismatch * ANGLE_SIZE[0] / width)
        assert alignment_px < .005, (key, alignment_px)
        title, source_crop, model_crop = focus.get(key, DETAILS[key]) if focus else DETAILS[key]
        detail_rotation, detail_center, detail_width = detail_view(view_cameras['Hero'], model_crop)
        lights = {obj: obj.location.copy() for obj in scene.objects if obj.type == 'LIGHT'}
        scene.render.resolution_percentage = 100
        scene.render.image_settings.color_mode = 'RGBA'
        scene.render.film_transparent = True
        swatches = FIXED_SWATCHES[key]
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots if 'Finish' in slot.material}
        for label, color in swatches:
            assert material_swatch_present(materials, color), (key, label, color)
        files = {}
        for team, model, shift in zip(('Blue', 'Red'), (blue, red), shifts, strict=True):
            for obj in scene.objects:
                if obj.type == 'MESH':
                    obj.hide_render = obj != model
            for light, location in lights.items():
                light.location = location + Vector(shift - shifts[0])
            files[team] = {}
            for angle, (rotation, center) in views.items():
                path = output / f'{key}_{team}_{angle}.png'
                render(scene, path, ANGLE_SIZE, rotation, center + Vector(shift), width)
                files[team][angle] = path.name
            path = output / f'{key}_{team}_Detail.png'
            render(scene, path, DETAIL_SIZE, detail_rotation, detail_center + Vector(shift), detail_width)
            files[team]['Detail'] = path.name
        assert hashlib.sha256((REFERENCES / refs[key]['file']).read_bytes()).hexdigest() == refs[key]['sha256']
        entries.append({'key': key, 'name': refs[key]['name'], 'model_id': row['model_id'], 'triangles': row['triangles'],
                        'team_areas': TEAM_AREAS[key], 'fixed_swatches': swatches, 'files': files,
                        'source': refs[key]['file'], 'source_sha256': refs[key]['sha256'], 'source_crop': source_crop,
                        'detail_title': title, 'model_detail_crop_px': model_crop,
                        'angle_width_m': round(width, 5), 'detail_width_m': round(detail_width, 5),
                        'red_blue_projection_error_px': round(alignment_px, 6),
                        'rotation_euler': {angle: view_cameras[angle][1] for angle in ANGLES},
                        'exposure': scene.view_settings.exposure, 'view_transform': scene.view_settings.view_transform})
        print(key, '8 angles and 2 close-ups rendered', flush=True)
    assert hashlib.sha256(project.read_bytes()).hexdigest() == source_sha
    revision = project.stem.rsplit('_', 1)[-1]
    metadata = {f'{revision.lower()}_sha256': source_sha, 'project': project.name, 'revision': revision, 'models': entries}
    (output / 'review_renders.json').write_text(json.dumps(metadata,
                                                          ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--') + 1:]
    build(Path(args[0]), Path(args[1]).resolve() if len(args) > 1 else DEST,
          read_json(Path(args[2])) if len(args) > 2 else None)
