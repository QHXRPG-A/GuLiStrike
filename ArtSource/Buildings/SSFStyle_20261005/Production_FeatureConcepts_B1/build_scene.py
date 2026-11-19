"""Build the SSF A5 material review in Blender from the formal LOD0 exports."""
import collections
import hashlib
import json
import math
import os
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'Source'
REFERENCES = ROOT.parent / 'References_FeatureConcepts_A5'
A4 = ROOT.parent / 'References_FacilityCoatings_A4' / 'SSF_FacilityCoatings_Reference_A4.blend'
NAMES = (
    ('AirBase', '空军基地'),
    ('CloningCenter', '克隆中心'),
    ('CommandCenter', '指挥中心'),
    ('MilitaryFactory', '军工厂'),
    ('Reactor', '反应堆'),
    ('StrategyCenter', '战略中心'),
)
TEAMS = {'Blue': ('#6AA4BE', '#274E61'), 'Red': ('#A34053', '#662249')}
REST = 'SSF Rest Coordinates'
PART = 'SSF Panel Coordinates'
CONTOUR_DISTANCE = 'SSF Main Panel Contour Distance'
STYLE_PROPERTY = 'SSF Stylization'
LIGHT_BANDS = (.40, .68, 1.)
ART_LIGHT = Vector((-.72, -.29, .63)).normalized()
AIR_DOORS = {'polymsh049', 'polymsh045'}
AIR_CROWN = {'polymsh050', 'polymsh046'}
AIR_SIDE = {'polymsh19', 'polymsh052'}
AIR_ACTUATORS = {'polymsh17', 'polymsh051'}
CAPSULES = {f'Capsule{i}' for i in range(1, 6)}
DUCTS = {f'Tube{i}' for i in range(1, 5)}
SHUTTERS = {f'DoorPart{i}' for i in range(1, 5)}
CONTROL_PLATFORMS = {'LBlock', 'LBlock1', 'LBlock2', 'LBlock3', 'LBlock4', 'LBlock5'}
RADIATOR_SEGMENTS = {'Block1', 'Block2', 'Block3'}
ARRAY_TILES = {f'0_cube{i}' for i in range(2, 47)}
ARRAY_BOXES = {'0_cube81', '0_cube80', '0_cube79', '0_cube60'}
# Retain the largest existing outline islands of these primary volumes.
MAJOR_OUTLINE_PARTS = {
    'AirBase': dict.fromkeys(('polymsh19', 'polymsh052', 'polymsh22', 'polymsh23'), 1),
    'CloningCenter': {'BackPart': 1, 'MainBlock': 2, 'Basis': 1, 'LeftPart': 1, 'RightPart': 1},
    'CommandCenter': dict.fromkeys(('MainBlock', 'Tower', 'TowerBase', 'BackBlock', 'Ring1'), 1),
    'MilitaryFactory': dict.fromkeys(('CivilBlock_L', 'CivilBlock_R', 'MainBlock', 'PartStructure1'), 1),
    'Reactor': dict.fromkeys(sorted(RADIATOR_SEGMENTS), 1),
    'StrategyCenter': dict.fromkeys(('0_polymsh7', '0_cube74', '0_polymsh21'), 1),
}
# Each entry targets the largest surface of a substantial mechanical part.
MAIN_PANEL_PARTS = {
    'AirBase': AIR_CROWN | AIR_DOORS,
    'CloningCenter': {'Column'},
    'CommandCenter': {'Ring', 'Ring2'},
    'MilitaryFactory': {'MainBlock', 'PartStructure2', 'CivilBlock_M'},
    'Reactor': RADIATOR_SEGMENTS | {'Hatch'},
    'StrategyCenter': {'0_cylinder', '0_cube47', '0_cube48', '0_polymsh21'},
}


@dataclass(frozen=True)
class Finish:
    label: str
    color: str
    roughness: float
    metallic: float = 0.0
    coat: float = 0.0
    reflection_strength: float = .062


FINISHES = {
    'ivory': Finish('Ivory painted cladding', '#FEE4D9', .52, .16),
    'sand': Finish('Sand painted steel', '#D5C09C', .48, .27),
    'silver': Finish('Pale machined alloy', '#FEE4D9', .34, .64, .14, .096),
    'sage': Finish('Gray-green powder coat', '#557B78', .54, .24),
    'graphite': Finish('Graphite steel', '#2C3735', .43, .34),
    'rubber': Finish('Graphite rubber', '#2C3735', .72, .02),
    'dark': Finish('Recess and rubber seal', '#1A182F', .71, .03),
    'amber': Finish('Amber guards and grips', '#EE9D58', .4, .21, .16),
    'primary': Finish('Faction primary coating', '', .49, .23, .14),
    'secondary': Finish('Faction secondary coating', '', .48, .25, .12),
    'shoulder': Finish('Faction equipment cover with pale flange', '', .48, .23, .14),
    'cap': Finish('Faction service cap with narrow collar', '', .49, .22, .13),
    'glass': Finish('Cultivation viewport and graphite collar', '#2C3735', .42, .22, .13, .082),
    'upper_ring': Finish('Command observation glass and pale rims', '#1A182F', .23, .26, .26, .089),
    'lower_ring': Finish('Command status strip and pale rim', '#2C3735', .36, .31, .18, .078),
    'duct': Finish('Graphite duct and pale metal couplings', '#2C3735', .44, .37, .13, .084),
    'radiator': Finish('Gray-green cooling blade and teal lip', '#557B78', .4, .43, .14, .08),
    'reactor_lock': Finish('Graphite lock with amber grip', '#2C3735', .46, .25),
    'array': Finish('Graphite RF ceramic with ivory edge', '#2C3735', .63, .13),
    'rear_shell': Finish('Cloning rear service cladding', '#FEE4D9', .55, .15),
    'jamb': Finish('Loading jamb with amber guard', '#D5C09C', .48, .30),
    'guide_rail': Finish('Hangar guide rail with graphite track and pale crossbars', '#FEE4D9', .51, .22),
    'pedestal': Finish('Gray-green pedestal with fixed teal datum', '#557B78', .54, .24),
}
TEAM_FINISHES = {'primary', 'secondary', 'shoulder', 'cap', 'rear_shell'}


def linear_color(hex_color):
    def channel(offset):
        srgb = int(hex_color[offset:offset + 2], 16) / 255
        return srgb / 12.92 if srgb <= .04045 else ((srgb + .055) / 1.055) ** 2.4

    return tuple(channel(i) for i in (1, 3, 5)) + (1.0,)


class Nodes:
    """Small helpers for the shared material graph and its part masks."""

    def __init__(self, material):
        material.use_nodes = True
        self.tree = material.node_tree
        self.tree.nodes.clear()

    def new(self, node_type, label=None):
        node = self.tree.nodes.new(node_type)
        if label:
            node.label = label
        return node

    def put(self, socket, value):
        if isinstance(value, bpy.types.NodeSocket):
            self.tree.links.new(value, socket)
        else:
            socket.default_value = value

    def math(self, operation, a, b=0.0):
        node = self.new('ShaderNodeMath')
        node.operation = operation
        self.put(node.inputs[0], a)
        self.put(node.inputs[1], b)
        return node.outputs[0]

    def mix(self, factor, first, second, mode='MIX', label=None):
        node = self.new('ShaderNodeMixRGB', label)
        node.blend_type = mode
        for socket, value in zip(node.inputs, (factor, first, second)):
            self.put(socket, value)
        return node.outputs[0]

    def separate(self, attribute):
        values = self.new('ShaderNodeAttribute', attribute)
        values.attribute_name = attribute
        axes = self.new('ShaderNodeSeparateXYZ')
        self.put(axes.inputs[0], values.outputs['Vector'])
        return axes.outputs

    def interval(self, value, low, high):
        return self.math('MULTIPLY', self.math('GREATER_THAN', value, low),
                         self.math('LESS_THAN', value, high))

    def rect(self, x, z, center_x, center_z, half_x, half_z, corner):
        dx = self.math('ABSOLUTE', self.math('SUBTRACT', x, center_x))
        dz = self.math('ABSOLUTE', self.math('SUBTRACT', z, center_z))
        sides = self.math('MULTIPLY', self.math('LESS_THAN', dx, half_x),
                          self.math('LESS_THAN', dz, half_z))
        corner_cut = self.math('LESS_THAN', self.math('ADD', dx, dz), half_x + half_z - corner)
        return self.math('MULTIPLY', sides, corner_cut)

    def arrange(self, output):
        levels = {output: 0}
        queue = [output]
        while queue:
            node = queue.pop(0)
            for inp in node.inputs:
                for link in inp.links:
                    previous = link.from_node
                    level = levels[node] + 1
                    if levels.get(previous, -1) < level:
                        levels[previous] = level
                        queue.append(previous)
        columns = collections.defaultdict(int)
        for node in self.tree.nodes:
            level = levels.get(node, 0)
            node.location = (-224 * level, -152 * columns[level])
            columns[level] += 1


def material_layers(graph, finish_id, base, team):
    """The visible A5 material borders live on the original surfaces."""
    finish = FINISHES[finish_id]
    roughness, metal, coat = finish.roughness, finish.metallic, finish.coat
    emission = (0., 0., 0., 1.)
    glass_strength = 0.
    transmission = 0.
    ivory = linear_color(FINISHES['ivory'].color)
    jade = linear_color('#0D9099')
    if finish_id == 'glass':
        u, v, w = graph.separate(PART)
        top = graph.math('GREATER_THAN', w, .77)
        side = graph.math('GREATER_THAN', graph.math('ABSOLUTE', graph.math('SUBTRACT', v, .5)), .33)
        side = graph.math('MULTIPLY', side, graph.interval(w, .31, .79))
        viewing_face = graph.math('MAXIMUM', top, side)
        window = graph.math('MULTIPLY', viewing_face, graph.interval(u, .19, .81))
        upper_lock = graph.math('MULTIPLY', top, graph.math('MAXIMUM',
            graph.interval(u, .105, .20), graph.interval(u, .80, .895)))
        base = graph.mix(window, base, jade, label='Frosted jade viewport')
        base = graph.mix(upper_lock, base, ivory, label='Ivory locking clips')
        roughness = graph.math('SUBTRACT', roughness, graph.math('MULTIPLY', window, .21))
        coat = graph.math('ADD', coat, graph.math('MULTIPLY', window, .37))
        transmission = graph.math('MULTIPLY', window, .18)
        emission, glass_strength = jade, graph.math('MULTIPLY', window, .33)
    elif finish_id == 'upper_ring':
        _, _, z = graph.separate(REST)
        window = graph.interval(z, 8.74, 9.30)
        base = graph.mix(window, ivory, base, label='Glazing between pale rims')
        roughness = graph.math('SUBTRACT', .47, graph.math('MULTIPLY', window, .27))
        coat = graph.math('MULTIPLY', window, .38)
        transmission = graph.math('MULTIPLY', window, .11)
    elif finish_id == 'lower_ring':
        x, y, z = graph.separate(REST)
        rim = graph.math('MAXIMUM', graph.math('LESS_THAN', z, 5.69), graph.math('GREATER_THAN', z, 6.38))
        base = graph.mix(rim, base, ivory, label='Pale rim around status deck')
        window = graph.math('SUBTRACT', 1., rim)
        roughness = graph.math('SUBTRACT', roughness, graph.math('MULTIPLY', window, .15))
        metal = graph.math('SUBTRACT', metal, graph.math('MULTIPLY', window, .16))
        coat = graph.math('ADD', coat, graph.math('MULTIPLY', window, .17))
        transmission = graph.math('MULTIPLY', window, .075)
        angle = graph.math('ARCTAN2', y, x)
        segments = graph.math('FRACT', graph.math('MULTIPLY', angle, 12 / (2 * math.pi)))
        slots = graph.interval(segments, .20, .72)
        line = graph.math('MAXIMUM', graph.interval(z, 5.98, 6.035), graph.interval(z, 6.09, 6.125))
        light = graph.math('MULTIPLY', slots, line)
        base = graph.mix(light, base, jade, label='Independent status slots')
        emission, glass_strength = jade, graph.math('MULTIPLY', light, .36)
    elif finish_id == 'duct':
        _, _, z = graph.separate(PART)
        collar = graph.math('MAXIMUM', graph.math('LESS_THAN', z, .125),
                            graph.math('GREATER_THAN', z, .902))
        base = graph.mix(collar, base, ivory, label='Metal couplings at both ends')
        metal = graph.math('ADD', metal, graph.math('MULTIPLY', collar, .26))
    elif finish_id == 'radiator':
        _, depth, z = graph.separate(PART)
        lip = graph.math('MULTIPLY', graph.math('GREATER_THAN', depth, .77),
                         graph.interval(z, .21, .48))
        base = graph.mix(lip, base, jade, label='Painted heat exchanger lip')
    elif finish_id == 'reactor_lock':
        _, _, z = graph.separate(PART)
        grip = graph.math('GREATER_THAN', z, .62)
        base = graph.mix(grip, base, linear_color('#EE9D58'), label='Amber grip')
    elif finish_id == 'array':
        u, v, _ = graph.separate(PART)
        face = graph.math('MULTIPLY', graph.interval(u, .04, .96), graph.interval(v, .055, .945))
        base = graph.mix(face, ivory, base, label='Ceramic tile inset in pale edge')
    elif finish_id == 'shoulder':
        _, _, z = graph.separate(REST)
        lip = graph.interval(z, 2.045, 2.16)
        base = graph.mix(lip, base, ivory, label='Pale equipment flange')
    elif finish_id == 'cap':
        _, _, z = graph.separate(REST)
        lip = graph.interval(z, 7.735, 7.93)
        base = graph.mix(lip, base, linear_color(TEAMS[team][0]), label='Narrow faction collar')
    elif finish_id == 'rear_shell':
        x, y, z = graph.separate(REST)
        paired_x = graph.math('ABSOLUTE', x)
        rear = graph.math('GREATER_THAN', y, 2.38)
        panels = (
            (x, 0, 4.84, .715, 1.915, .15, '#D5C09C', 'Rear equipment spine'),
            (paired_x, 1.92, 3.81, .61, 1.04, .16, '#D5C09C', 'Rear access covers'),
            (x, 0, 1.78, .625, .695, .13, '#557B78', 'Rear equipment hatch'),
            (paired_x, 1.70, 1.42, .53, .79, .14, '#557B78', 'Lower support covers'),
            (paired_x, 1.29, 5.81, .355, .655, .14, TEAMS[team][0], 'Paired shoulder covers'),
        )
        for coord, cx, cz, hw, hh, cut, color, label in panels:
            mask = graph.math('MULTIPLY', rear, graph.rect(coord, z, cx, cz, hw, hh, cut))
            base = graph.mix(mask, base, linear_color(color), label=label)
    elif finish_id == 'jamb':
        x, _, z = graph.separate(REST)
        sides = graph.interval(graph.math('ABSOLUTE', x), 2.07, 2.56)
        guard = graph.math('MULTIPLY', sides, graph.interval(z, .32, 1.55))
        base = graph.mix(guard, base, linear_color('#EE9D58'), label='Fixed amber jamb guards')
    elif finish_id == 'guide_rail':
        x, y, z = graph.separate(REST)
        run = graph.math('ABSOLUTE', y)
        width = graph.math('LESS_THAN', graph.math('ABSOLUTE', x), .245)
        track = graph.math('MULTIPLY', width, graph.interval(run, 6.53, 8.34))
        track = graph.math('MULTIPLY', track, graph.math('GREATER_THAN', z, 3.76))
        pitch = graph.math('MULTIPLY', graph.math('SUBTRACT', run, 6.53), 5. / 1.81)
        bar = graph.interval(graph.math('FRACT', pitch), .19, .44)
        insert = graph.mix(bar, linear_color('#2C3735'), linear_color('#D5C09C'))
        base = graph.mix(track, base, insert, label='Graphite track and pale crossbars')
        roughness = graph.math('SUBTRACT', roughness, graph.math('MULTIPLY', track, .10))
    elif finish_id == 'pedestal':
        x, y, z = graph.separate(REST)
        squared_radius = graph.math('ADD', graph.math('MULTIPLY', x, x), graph.math('MULTIPLY', y, y))
        radius = graph.math('SQRT', squared_radius)
        angle = graph.math('ABSOLUTE', graph.math('ARCTAN2', x, graph.math('MULTIPLY', y, -1.)))
        arcs = graph.math('MULTIPLY', graph.interval(radius, 2.07, 2.27), graph.interval(angle, .055, .37))
        arcs = graph.math('MULTIPLY', arcs, graph.interval(z, 3.81, 4.3))
        base = graph.mix(arcs, base, linear_color('#0A6F73'), label='Two painted teal datum arcs')
    return base, roughness, metal, coat, emission, glass_strength, transmission


def scene_style(output):
    output.default_value = 1.
    driver = output.driver_add('default_value').driver
    driver.type = 'AVERAGE'
    variable = driver.variables.new()
    variable.name = 'scene_style'
    variable.type = 'CONTEXT_PROP'
    target = variable.targets[0]
    target.context_property = 'ACTIVE_SCENE'
    target.data_path = f'["{STYLE_PROPERTY}"]'
    return output


def make_material(name, finish_id, team):
    finish = FINISHES[finish_id]
    is_team = finish_id in TEAM_FINISHES
    material = bpy.data.materials.new(f'{name} | {finish.label}' + (f' | {team}' if is_team else ''))
    color = finish.color or TEAMS[team][int(finish_id in ('secondary', 'cap'))]
    material.diffuse_color = linear_color(color)
    material['Base sRGB'] = color
    material['Finish'] = finish.label
    material['Faction'] = team if is_team else 'Fixed'
    material.roughness = finish.roughness
    material.metallic = finish.metallic
    graph = Nodes(material)
    tint = graph.new('ShaderNodeRGB', 'Editable base color')
    tint.outputs[0].default_value = linear_color(color)
    base, roughness, metal, coat, emission_color, emission_strength, transmission = material_layers(
        graph, finish_id, tint.outputs[0], team)
    style = scene_style(graph.new('ShaderNodeValue', 'Scene Properties | SSF Stylization').outputs[0])

    normal = graph.new('ShaderNodeNewGeometry').outputs['Normal']
    light = graph.new('ShaderNodeVectorMath', 'Directional three-tone shading')
    light.operation = 'DOT_PRODUCT'
    graph.put(light.inputs[0], normal)
    light.inputs[1].default_value = ART_LIGHT
    shadow_value, middle_value, light_value = LIGHT_BANDS
    mid = graph.math('MULTIPLY', graph.math('GREATER_THAN', light.outputs['Value'], .33), middle_value - shadow_value)
    high = graph.math('MULTIPLY', graph.math('GREATER_THAN', light.outputs['Value'], .69), light_value - middle_value)
    band = graph.math('ADD', graph.math('ADD', shadow_value, mid), high)
    painted = graph.mix(1., base, band, 'MULTIPLY')

    edge_x, edge_y, edge_z = graph.separate(CONTOUR_DISTANCE)
    distance = graph.math('MINIMUM', edge_x, graph.math('MINIMUM', edge_y, edge_z))
    contour = graph.new('ShaderNodeMapRange', 'Main panel contours')
    contour.interpolation_type = 'SMOOTHSTEP'
    contour.clamp = True
    graph.put(contour.inputs['Value'], distance)
    for socket_name, value in (('From Min', .44), ('From Max', 1.12), ('To Min', .83), ('To Max', 0.)):
        contour.inputs[socket_name].default_value = value
    painted = graph.mix(contour.outputs['Result'], painted, linear_color('#1A182F'), label='Major contour ink')

    if not isinstance(emission_strength, (int, float)):
        painted = graph.mix(emission_strength, painted, emission_color, 'ADD')

    illustration = graph.new('ShaderNodeEmission', 'Three-tone color')
    graph.put(illustration.inputs[0], painted)
    surface = graph.new('ShaderNodeBsdfPrincipled', 'Editable physical finish')
    for key, value in (('Base Color', base), ('Roughness', roughness),
                       ('Metallic', metal), ('Coat Weight', coat),
                       ('Emission Color', emission_color), ('Emission Strength', emission_strength),
                       ('Transmission Weight', transmission)):
        graph.put(surface.inputs[key], value)
    # Let the artistic light determine the diffuse band. Keep only a small
    # reflection term here so the material retains its own surface finish.
    reflection = graph.new('ShaderNodeBsdfAnisotropic', 'Surface reflection')
    reflection_tint = graph.mix(metal, (.85, .85, .85, 1.), base)
    weight = graph.math('MULTIPLY', finish.reflection_strength, graph.math('SUBTRACT', 1., contour.outputs['Result']))
    graph.put(reflection.inputs['Color'], graph.mix(1., reflection_tint, weight, 'MULTIPLY'))
    graph.put(reflection.inputs['Roughness'], roughness)
    stylized = graph.new('ShaderNodeAddShader', 'Three diffuse bands and surface reflection')
    graph.put(stylized.inputs[0], illustration.outputs[0])
    graph.put(stylized.inputs[1], reflection.outputs[0])
    result = graph.new('ShaderNodeMixShader', 'Pure material / three bands')
    graph.put(result.inputs[0], style)
    graph.put(result.inputs[1], surface.outputs[0])
    graph.put(result.inputs[2], stylized.outputs[0])
    out = graph.new('ShaderNodeOutputMaterial')
    graph.put(out.inputs['Surface'], result.outputs[0])
    graph.arrange(out)
    return material


def choose_finish(name, part, role, point, normal, size, is_blade):
    x, y, z = point
    if name == 'AirBase':
        if part in AIR_DOORS:
            return {0: 'guide_rail', 1: 'sand', 2: 'graphite', 3: 'sand'}[role]
        if part in AIR_CROWN:
            return 'dark' if role == 4 else 'ivory'
        if part in AIR_SIDE:
            return 'graphite' if role == 2 else 'ivory'
        if part in AIR_ACTUATORS:
            return 'primary' if role in (1, 3) else 'graphite'
        if part == 'polymsh23':
            return 'secondary' if role == 4 else 'graphite'
        if part == 'cube30':
            return 'primary' if role == 4 else 'ivory'
        if part == 'cube16':
            return 'secondary' if role == 1 else 'graphite'
        if part == 'cube48':
            return 'ivory' if role in (0, 1) else 'primary'
        if part in ('cube124', 'cube130'):
            return 'amber'
        if part in ('Anten0', 'cube85', 'cube74', 'cube81'):
            return 'graphite'
        return {0: 'ivory', 1: 'sand', 2: 'sage', 3: 'ivory', 4: 'secondary'}.get(role, 'graphite')

    if name == 'CloningCenter':
        if part in CAPSULES:
            return 'glass'
        if part.startswith('BoneHose'):
            return 'rubber'
        if part in ('LeftPart', 'RightPart'):
            return 'secondary' if role == 4 else 'shoulder'
        if part == 'Column':
            return 'cap' if role in (0, 4) else 'ivory'
        if part == 'BackPart':
            return 'rear_shell' if role == 1 else 'sage'
        if part == 'MainBlock':
            if role == 2:
                return 'graphite'
            if role == 1 and abs(x) > .94 and abs(normal.z) < .72:
                return 'sand'
            return 'ivory'
        if part in ('Door', 'Display', 'Vent1', 'Vent2'):
            return 'graphite'
        return 'sage' if part.startswith('Box') else 'sand'

    if name == 'CommandCenter':
        if part == 'Ring':
            return 'upper_ring'
        if part in ('Ring1', 'Ring2'):
            return 'lower_ring' if part == 'Ring2' else 'graphite'
        if part == 'Tower':
            return 'sand' if z > 10.02 or role == 1 else 'sage'
        if part == 'BlockA':
            return 'primary'
        if part in ('BlockL', 'BlockR'):
            return 'primary' if normal.z > .32 and z > 3.55 else 'sage'
        if part in CONTROL_PLATFORMS:
            return 'primary' if normal.z > .72 else 'ivory'
        if part.startswith(('Bracket', 'Rack')):
            return 'sage' if z > .64 else 'graphite'
        if part in ('MainBlock', 'BackBlock'):
            return 'sand' if role != 2 else 'graphite'
        return 'graphite' if part.startswith(('Tube', 'Cable', 'Duct', 'Antenna', 'Radar', 'Vent', 'Weap')) else 'ivory'

    if name == 'MilitaryFactory':
        if part in SHUTTERS:
            return 'sand' if role == 0 else 'graphite'
        if part in DUCTS or part.startswith('Pipe'):
            return 'duct'
        if part.startswith(('Vent', 'Antenna', 'Damper')):
            return 'graphite'
        if part in ('PartStructure1', 'PartStructure2'):
            return 'primary'
        if part == 'CivilBlock_M':
            return 'secondary'
        if part == 'MainBlock':
            if y < -3.6 and abs(x) > 1.9 and normal.y < -.30:
                return 'jamb'
            return 'secondary' if role == 4 else ('ivory' if role == 0 else 'sage')
        if part.startswith('CivilBlock'):
            return 'secondary' if role == 4 else 'sage'
        if part.startswith('RoboLeg'):
            return 'graphite'
        if part.startswith('Light'):
            return 'ivory' if role == 0 else 'graphite'
        return 'sage'

    if name == 'Reactor':
        if part == 'Rod':
            return 'reactor_lock'
        if part == 'Hatch':
            return 'secondary' if role == 4 else 'graphite'
        if part == 'Airing' or part == 'Gears':
            return 'graphite'
        if part in RADIATOR_SEGMENTS:
            if is_blade:
                return 'radiator'
            if role == 0:
                return 'ivory' if z > 1.34 else 'sand'
            if role == 1:
                return 'ivory'
            if role == 4:
                return 'sand'
            if role == 3:
                return 'ivory' if normal.z > .57 and z > 3.60 else 'primary'
        return 'graphite'

    if name == 'StrategyCenter':
        if part in ARRAY_TILES and size == 8:
            return 'array' if normal.y < -.75 and normal.z > .25 else 'ivory'
        if part in ARRAY_BOXES:
            return 'primary' if normal.z > .20 else 'secondary'
        if part in ('0_cube47', '0_cube48'):
            return 'sage' if role == 2 else 'secondary'
        if part == '0_polymsh21':
            return 'ivory' if role == 0 else 'graphite'
        if part in ('0_polymsh7', '0_cylinder'):
            return 'graphite' if part == '0_cylinder' or role == 2 else 'pedestal'
        if part.startswith(('0_Cube', '0_Cylinder', 'Vent')):
            return 'graphite'
        if part in ('0_cube74', '0_cube78', '0_cube100', '0_cube103'):
            return 'sand' if role != 2 else 'graphite'
        return {0: 'ivory', 1: 'sand', 2: 'graphite', 3: 'primary', 4: 'sage'}.get(role, 'graphite')
    raise ValueError(name)


def component_faces(mesh, body, parts, positions, normals=None, roles=None):
    """Weld UV and normal splits for analysis without editing topology."""
    keys = [tuple(round(v * 10000) for v in point) for point in positions]
    edges = collections.defaultdict(list)
    parents = {face: face for face in body}
    for face in body:
        for a, b in mesh.polygons[face].edge_keys:
            if keys[a] != keys[b]:
                edge = (parts[face], *sorted((keys[a], keys[b])))
                edges[edge].append(face)

    def root(face):
        while face != parents[face]:
            parents[face] = parents[parents[face]]
            face = parents[face]
        return face

    for candidates in edges.values():
        if len(candidates) < 2:
            continue
        first = candidates[0]
        for face in candidates[1:]:
            if normals is not None and (roles[first] != roles[face] or normals[first].dot(normals[face]) < .997):
                continue
            parents[root(face)] = root(first)
    result = collections.defaultdict(list)
    for face in body:
        result[root(face)].append(face)
    return result.values()


def original_parts(obj):
    names = {group.index: group.name for group in obj.vertex_groups}
    return [names[max(vertex.groups, key=lambda weight: weight.weight).group] if vertex.groups else ''
            for vertex in obj.data.vertices]


def write_coordinates(mesh, positions, vertices_by_part, name, radiator_blades):
    rest = mesh.attributes.new(REST, 'FLOAT_VECTOR', 'POINT')
    rest.data.foreach_set('vector', [value for point in positions for value in point])
    coordinate = np.zeros((len(positions), 3), dtype=np.float32)
    for group, ids in vertices_by_part.items():
        points = np.asarray([positions[i][:] for i in ids])
        unique = np.unique(np.round(points, 6), axis=0)
        basis = np.eye(3)
        if name == 'CloningCenter' and group in CAPSULES:
            _, _, axes = np.linalg.svd(unique[:, :2] - unique[:, :2].mean(axis=0))
            forward = axes[0]
            basis = np.array(((forward[0], -forward[1], 0), (forward[1], forward[0], 0), (0, 0, 1)))
        if name == 'StrategyCenter' and group in ARRAY_TILES and len(unique) == 8:
            basis = np.array(((1., 0., 0.), (0., .5, -.8660254), (0., .8660254, .5)))
        projected = points @ basis
        low, high = projected.min(axis=0), projected.max(axis=0)
        coordinate[ids] = (projected - low) / np.maximum(high - low, .0001)
    for blades in radiator_blades.values():
        islands = [sorted({i for face in blade for i in mesh.polygons[face].vertices}) for blade in blades]
        bank = np.asarray([positions[i][:2] for island in islands for i in island])
        outward = bank.mean(axis=0)
        outward /= np.linalg.norm(outward)
        x, y = outward
        basis = np.array(((y, x, 0.), (-x, y, 0.), (0., 0., 1.)))
        for ids in islands:
            points = np.asarray([positions[i][:] for i in ids])
            projected = points @ basis
            low, high = projected.min(axis=0), projected.max(axis=0)
            coordinate[ids] = (projected - low) / np.maximum(high - low, .0001)
    panel = mesh.attributes.new(PART, 'FLOAT_VECTOR', 'POINT')
    panel.data.foreach_set('vector', coordinate.ravel())


def select_outline_faces(name, mesh, positions, vertex_parts):
    shell = [face.index for face in mesh.polygons if face.material_index == 2]
    parts = {i: collections.Counter(vertex_parts[v] for v in mesh.polygons[i].vertices).most_common(1)[0][0]
             for i in shell}
    islands = collections.defaultdict(list)
    for component in component_faces(mesh, shell, parts, positions):
        islands[parts[component[0]]].append(component)
    retained = {}
    for part, count in MAJOR_OUTLINE_PARTS[name].items():
        ranked = sorted(islands[part], key=lambda island: sum(mesh.polygons[i].area for i in island),
                        reverse=True)
        assert len(ranked) >= count, (name, part, count)
        retained[part] = [i for island in ranked[:count] for i in island]
    visible = {i for faces in retained.values() for i in faces}
    return visible, {'source_faces': len(shell), 'major_faces': len(visible),
                     'suppressed_faces': len(shell) - len(visible),
                     'major_faces_by_part': {part: len(faces) for part, faces in retained.items()}}


def contour_paths(edges):
    """Follow the straightest unbranched contour through each junction."""
    touching = collections.defaultdict(list)
    for edge in edges:
        for vertex, tip in (edge, edge[::-1]):
            tangent = (Vector(tip) - Vector(vertex)).normalized()
            touching[vertex].append((edge, tangent))
    links = collections.defaultdict(list)
    for candidates in touching.values():
        choices = sorted((first_direction.dot(second_direction), first, second)
                         for i, (first, first_direction) in enumerate(candidates)
                         for second, second_direction in candidates[i + 1:])
        matched = set()
        for turn, first, second in choices:
            if turn > .20:
                break
            if first not in matched and second not in matched:
                links[first].append(second)
                links[second].append(first)
                matched.update((first, second))
    visited = set()
    paths = []
    for start in edges:
        if start in visited:
            continue
        path = [start]
        visited.add(start)
        for edge in path:
            for neighbor in links[edge]:
                if neighbor not in visited:
                    path.append(neighbor)
                    visited.add(neighbor)
        paths.append(path)
    return paths


def write_main_panel_contours(name, mesh, body, parts, positions, normals):
    target = MAIN_PANEL_PARTS[name]
    candidates = [i for i in body if parts[i] in target]
    islands = {}
    for island in component_faces(mesh, candidates, parts, positions):
        part = parts[island[0]]
        area = sum(mesh.polygons[i].area for i in island)
        if part not in islands or area > islands[part][0]:
            islands[part] = area, island
    assert set(islands) == target, (name, target - set(islands))

    span = max(max(p[axis] for p in positions) - min(p[axis] for p in positions) for axis in range(3))
    half_width = max(.020, span * .0034)
    vertex_keys = [tuple(round(v * 10000) for v in point) for point in positions]
    distances = np.full((len(mesh.loops), 3), 8., dtype=np.float32)
    report = {}
    for part, (_, island) in sorted(islands.items()):
        adjacent = collections.defaultdict(list)
        centers = {}
        for index in island:
            face = mesh.polygons[index]
            centers[index] = sum((positions[v] for v in face.vertices), Vector()) / len(face.vertices)
            for a, b in face.edge_keys:
                if vertex_keys[a] != vertex_keys[b]:
                    adjacent[tuple(sorted((vertex_keys[a], vertex_keys[b])))].append((index, a, b))
        lengths = {}
        for edge, faces in adjacent.items():
            if len(faces) != 2:
                continue
            (index, a, b), (other, _, _) = faces
            length = (positions[b] - positions[a]).length
            convex = (normals[index].dot(centers[other] - positions[a]) < -1e-5
                      and normals[other].dot(centers[index] - positions[a]) < -1e-5)
            if convex and length >= span * .013 and -.68 <= normals[index].dot(normals[other]) <= .73:
                lengths[edge] = length

        runs = [(sum(lengths[edge] for edge in path), path) for path in contour_paths(lengths)]
        longest = sorted(runs, key=lambda run: run[0], reverse=True)[:2]
        selected = [(length, path) for length, path in longest if length >= span * .15]
        if not selected:
            continue
        for _, path in selected:
            for edge in path:
                cross_sections = []
                for index, a, b in adjacent[edge]:
                    face = mesh.polygons[index]
                    opposite = next(i for i, vertex in enumerate(face.vertices) if vertex not in (a, b))
                    point = positions[face.vertices[opposite]]
                    height = (positions[b] - positions[a]).cross(point - positions[a]).length / lengths[edge]
                    cross_sections.append((face, opposite, height))
                width = min(half_width, min(section[2] for section in cross_sections) * .38)
                assert width > 0., (name, part, edge)
                for face, opposite, height in cross_sections:
                    for corner, loop in enumerate(face.loop_indices):
                        distances[loop, opposite] = height / width if corner == opposite else 0.
        report[part] = {'edges': sum(len(path) for _, path in selected),
                        'run_lengths_m': [round(length, 4) for length, _ in selected]}
    assert report and np.isfinite(distances).all(), name
    attribute = mesh.attributes.new(CONTOUR_DISTANCE, 'FLOAT_VECTOR', 'CORNER')
    attribute.data.foreach_set('vector', distances.ravel())
    return {'max_half_width_m': half_width, 'parts': report}


def geometry_digest(obj, old_face_classes):
    mesh = obj.data
    digest = hashlib.sha256()
    for vertex in mesh.vertices:
        digest.update(struct.pack('<3f', *vertex.co))
        for group in vertex.groups:
            digest.update(struct.pack('<If', group.group, group.weight))
    for face, original_class in zip(mesh.polygons, old_face_classes):
        digest.update(struct.pack('<2I', original_class, len(face.vertices)))
        digest.update(struct.pack(f'<{len(face.vertices)}I', *face.vertices))
    for uv in mesh.uv_layers:
        digest.update(uv.name.encode())
        for loop in uv.data:
            digest.update(struct.pack('<2f', *loop.uv))
    return digest.hexdigest()


def classify_asset(name, obj):
    mesh = obj.data
    original = mesh.color_attributes['Attribute']
    positions = [obj.matrix_world @ vertex.co for vertex in mesh.vertices]
    normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
    vertex_parts = original_parts(obj)
    vertices_by_part = collections.defaultdict(list)
    for index, group in enumerate(vertex_parts):
        vertices_by_part[group].append(index)
    size = {group: len({tuple(round(c, 5) for c in positions[i]) for i in ids})
            for group, ids in vertices_by_part.items()}
    body = [face.index for face in mesh.polygons if face.material_index == 0]
    parts = {i: collections.Counter(vertex_parts[v] for v in mesh.polygons[i].vertices).most_common(1)[0][0]
             for i in body}
    roles = {i: round(original.data[mesh.polygons[i].loop_start].color[3] * 255) for i in body}
    normals = {i: (normal_matrix @ mesh.polygons[i].normal).normalized() for i in body}
    blade_faces = set()
    radiator_blades = collections.defaultdict(list)
    if name == 'Reactor':
        for component in component_faces(mesh, body, parts, positions):
            part = parts[component[0]]
            if part not in RADIATOR_SEGMENTS or len(component) != 96 or any(roles[i] != 2 for i in component):
                continue
            blade_faces.update(component)
            radiator_blades[part].append(component)
        blade_count = {part: len(blades) for part, blades in radiator_blades.items()}
        assert blade_count == {part: 3 for part in RADIATOR_SEGMENTS}, dict(blade_count)
    candidates = {}
    for i in body:
        face = mesh.polygons[i]
        candidates[i] = choose_finish(name, parts[i], roles[i], obj.matrix_world @ face.center,
                                      normals[i], size[parts[i]], i in blade_faces)
    # Color changes that cross a large coplanar panel are handled in the shader.
    for panel in component_faces(mesh, body, parts, positions, normals, roles):
        totals = collections.Counter()
        for i in panel:
            totals[candidates[i]] += mesh.polygons[i].area
        dominant = totals.most_common(1)[0][0]
        for i in panel:
            candidates[i] = dominant
    write_coordinates(mesh, positions, vertices_by_part, name, radiator_blades)
    visible_outline, outline_report = select_outline_faces(name, mesh, positions, vertex_parts)
    contour_report = write_main_panel_contours(name, mesh, body, parts, positions, normals)
    blade_count = {part: len(blades) for part, blades in radiator_blades.items()}
    return candidates, parts, positions, blade_count, visible_outline, outline_report, contour_report


def attach_to_scene(scene, collection):
    if collection.name not in scene.collection.children:
        scene.collection.children.link(collection)


def duplicate_assembly(imported, source_mesh, source_rig, target):
    copies = {}
    for obj in imported:
        copy = obj.copy()
        target.objects.link(copy)
        copies[obj] = copy
    for old, copy in copies.items():
        if old.parent in copies:
            copy.parent = copies[old.parent]
        for modifier in copy.modifiers:
            if modifier.type == 'ARMATURE' and modifier.object == source_rig:
                modifier.object = copies[source_rig]
    return copies[source_mesh], copies[source_rig], copies


def place_assembly(scene, objects, asset, team, offset):
    collection = bpy.data.collections.new(f'{asset} | {team} | Editable assembly')
    attach_to_scene(scene, collection)
    root = bpy.data.objects.new(f'{asset} | {team} | Placement', None)
    root.empty_display_size = .3
    root.location = offset
    collection.objects.link(root)
    all_objects = set(objects)
    for obj in objects:
        for old_collection in list(obj.users_collection):
            old_collection.objects.unlink(obj)
        collection.objects.link(obj)
        if obj.parent not in all_objects:
            obj.parent = root
    return collection


def stage_scene(scene, label):
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.resolution_percentage = 100
    scene.render.resolution_x, scene.render.resolution_y = 1920, 1230
    scene.eevee.taa_render_samples = 32
    scene.unit_settings.system = 'METRIC'
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    scene.view_settings.exposure = 0
    scene[STYLE_PROPERTY] = 0.
    scene.id_properties_ui(STYLE_PROPERTY).update(min=0., max=1., soft_min=0., soft_max=1.,
        description='0 = physical materials only. 1 = main structure contours and three light bands.')
    scene.world = bpy.data.worlds.new(f'{label} | neutral ambient')
    scene.world.use_nodes = True
    background = next(node for node in scene.world.node_tree.nodes if node.type == 'BACKGROUND')
    background.inputs[0].default_value = (.44, .45, .47, 1)
    background.inputs[1].default_value = .65


def light(scene, name, energy, size, location, aim):
    data = bpy.data.lights.new(name, 'AREA')
    data.energy = energy
    data.shape = 'DISK'
    data.size = size
    obj = bpy.data.objects.new(name, data)
    scene.collection.objects.link(obj)
    obj.location = location
    obj.rotation_euler = (aim - obj.location).to_track_quat('-Z', 'Y').to_euler()


def floor(scene, center, span, z, material):
    mesh = bpy.data.meshes.new(f'{scene.name} | Studio floor')
    mesh.from_pydata([(-span, -span, z), (span, -span, z), (span, span, z), (-span, span, z)], [], [(0, 1, 2, 3)])
    plane = bpy.data.objects.new('Neutral review floor', mesh)
    plane.location = (center.x, center.y, 0)
    mesh.materials.append(material)
    scene.collection.objects.link(plane)


def add_reference(scene, image, center, rotation, width, height):
    collection = bpy.data.collections.new(f'{scene.name} | A5 concept board')
    attach_to_scene(scene, collection)
    mesh = bpy.data.meshes.new(image.name + ' | reference board')
    mesh.from_pydata([(-width / 2, -height / 2, 0), (width / 2, -height / 2, 0),
                      (width / 2, height / 2, 0), (-width / 2, height / 2, 0)], [], [(0, 1, 2, 3)])
    uv = mesh.uv_layers.new()
    for index, point in enumerate(((0, 0), (1, 0), (1, 1), (0, 1))):
        uv.data[index].uv = point
    obj = bpy.data.objects.new('A5 reference | left blue, right red', mesh)
    collection.objects.link(obj)
    obj.location = center
    obj.rotation_euler = rotation.to_euler()
    obj.hide_select = True
    material = bpy.data.materials.new(image.name + ' | reference image')
    graph = Nodes(material)
    texture = graph.new('ShaderNodeTexImage')
    texture.image = image
    emission = graph.new('ShaderNodeEmission')
    graph.put(emission.inputs[0], texture.outputs['Color'])
    out = graph.new('ShaderNodeOutputMaterial')
    graph.put(out.inputs['Surface'], emission.outputs[0])
    graph.arrange(out)
    mesh.materials.append(material)
    return obj


def set_camera(scene, points, direction, image=None):
    view = Vector(direction).normalized()
    rotation = (-view).to_track_quat('-Z', 'Y')
    right = rotation @ Vector((1, 0, 0))
    up = rotation @ Vector((0, 1, 0))

    def limits(axis):
        samples = [point.dot(axis) for point in points]
        return min(samples), max(samples)

    left, right_edge = limits(right)
    bottom, top = limits(up)
    back, front = limits(view)
    width, height = right_edge - left, top - bottom
    mid_x, mid_depth = (left + right_edge) * .5, (back + front) * .5
    if image:
        reference_ratio = image.size[0] / image.size[1]
        board_width = min(width, height * reference_ratio * .94)
        board_height = board_width / reference_ratio
        gap = height * .055
        board_center = right * mid_x + up * (top + gap + board_height * .5) + view * (mid_depth - height * 1.2)
        floor_clearance = min(point.z for point in points) + .03
        board_low = board_center.z - abs(up.z) * board_height * .5
        if board_low < floor_clearance:
            board_center += view * ((floor_clearance - board_low) / view.z)
        add_reference(scene, image, board_center, rotation, board_width, board_height)
        top += gap + board_height
        scene.render.resolution_y = 1550
    data = bpy.data.cameras.new(scene.name + ' | Orthographic review')
    data.type = 'ORTHO'
    data.clip_end = 800.
    aspect = scene.render.resolution_x / scene.render.resolution_y
    data.ortho_scale = max(width, (top - bottom) * aspect) * 1.12
    camera = bpy.data.objects.new(data.name, data)
    scene.collection.objects.link(camera)
    center = right * mid_x + up * ((top + bottom) * .5) + view * mid_depth
    camera.location = center + view * max(35., width * 1.25)
    camera.rotation_euler = rotation.to_euler()
    scene.camera = camera


def mesh_bounds(obj):
    return [obj.matrix_world @ Vector(point) for point in obj.bound_box]


def prepare_scene(scene, objects, floor_material, concept=None):
    stage_scene(scene, scene.name)
    points = [point for obj in objects for point in mesh_bounds(obj)]
    low = Vector(min(point[i] for point in points) for i in range(3))
    high = Vector(max(point[i] for point in points) for i in range(3))
    center = (low + high) * .5
    span = max(high.x - low.x, high.y - low.y, high.z - low.z, 1)
    floor(scene, center, span * 4, low.z - .02, floor_material)
    light(scene, 'Soft studio key', span ** 2 * 27, span * .72, center + Vector((-span, -span, span * 1.6)), center)
    light(scene, 'Studio fill', span ** 2 * 8, span * 1.3, center + Vector((span, -span * .1, span)), center)
    light(scene, 'Pale edge reflection', span ** 2 * 16, span * .85, center + Vector((span * .2, span, span)), center)
    set_camera(scene, points, (-.57, -1., .83) if concept else (-.26, -1., 1.25), concept)


def verify_material_regions(name, parts, finishes, blade_count):
    by_part = collections.defaultdict(set)
    for index, part in parts.items():
        by_part[part].add(finishes[index])
    if name == 'AirBase':
        for part in AIR_DOORS:
            assert {'sand', 'guide_rail', 'graphite'} <= by_part[part]
        assert all('primary' in by_part[part] for part in AIR_ACTUATORS)
    elif name == 'CloningCenter':
        assert all(by_part[part] == {'glass'} for part in CAPSULES)
        assert all(by_part[part] == {'rubber'} for part in by_part if part.startswith('BoneHose'))
    elif name == 'CommandCenter':
        assert by_part['Ring'] == {'upper_ring'} and by_part['Ring2'] == {'lower_ring'}
    elif name == 'MilitaryFactory':
        assert all({'sand', 'graphite'} <= by_part[part] for part in SHUTTERS)
        assert all(by_part[part] == {'duct'} for part in DUCTS)
    elif name == 'Reactor':
        assert all(blade_count[part] == 3 and 'radiator' in by_part[part] for part in RADIATOR_SEGMENTS)
        assert by_part['Rod'] == {'reactor_lock'}
    elif name == 'StrategyCenter':
        assert sum('array' in finishes for finishes in by_part.values()) >= 30
        assert 'pedestal' in by_part['0_polymsh7']
    return {part: sorted(values) for part, values in sorted(by_part.items())}


def import_and_paint(scene, name, label, source_row, outline, hidden_outline):
    known = set(bpy.data.objects)
    filepath = SOURCE / f'{name}.fbx'
    bpy.ops.import_scene.fbx(filepath=str(filepath), use_custom_normals=True, colors_type='LINEAR')
    imported = sorted(set(bpy.data.objects) - known, key=lambda obj: obj.name)
    blue, = [obj for obj in imported if obj.type == 'MESH']
    rig, = [obj for obj in imported if obj.type == 'ARMATURE']
    mesh = blue.data
    original_classes = [face.material_index for face in mesh.polygons]
    assert set(original_classes) <= {0, 1, 2}
    original_digest = geometry_digest(blue, original_classes)
    finishes, parts, positions, blades, visible_outline, outline_report, contour_report = classify_asset(name, blue)
    assignments = verify_material_regions(name, parts, finishes, blades)
    assert geometry_digest(blue, original_classes) == original_digest
    dims = [max(point[i] for point in positions) - min(point[i] for point in positions) for i in range(3)]
    assert np.allclose(dims, np.asarray(source_row['bounds_cm'][1]) / 50., atol=.002)

    display = bpy.data.materials[f'{name} - original display']
    ids = sorted(set(finishes.values()))
    mesh.materials.clear()
    for finish in ids:
        mesh.materials.append(make_material(name, finish, 'Blue'))
    mesh.materials.append(display)
    mesh.materials.append(hidden_outline)
    mesh.materials.append(outline)
    slot_index = {value: index for index, value in enumerate(ids)}
    for face, old in zip(mesh.polygons, original_classes):
        if old == 0:
            face.material_index = slot_index[finishes[face.index]]
        elif old == 1:
            face.material_index = len(ids)
        else:
            face.material_index = len(ids) + 1 + int(face.index in visible_outline)
    assert geometry_digest(blue, original_classes) == original_digest

    staged_red = bpy.data.collections.new(f'{name} | temporary assembly links')
    attach_to_scene(scene, staged_red)
    red, red_rig, clones = duplicate_assembly(imported, blue, rig, staged_red)
    for finish in TEAM_FINISHES.intersection(ids):
        slot = red.material_slots[slot_index[finish]]
        slot.link = 'OBJECT'
        slot.material = make_material(name, finish, 'Red')

    index = [item[0] for item in NAMES].index(name)
    scene_center = Vector(((index % 2 - .5) * 52., (1 - index // 2) * 29., 0))
    low = Vector(min(point[i] for point in positions) for i in range(3))
    high = Vector(max(point[i] for point in positions) for i in range(3))
    center = (low + high) * .5
    view_right = Vector((1., -.57, 0.)).normalized()
    widths = [point.dot(view_right) for point in positions]
    separation = view_right * ((max(widths) - min(widths)) * .59)
    assembly_center = scene_center - Vector((center.x, center.y, low.z))
    blue_collection = place_assembly(scene, imported, name, 'Blue', assembly_center - separation)
    red_collection = place_assembly(scene, list(clones.values()), name, 'Red', assembly_center + separation)
    bpy.data.collections.remove(staged_red)
    for side, model, armature in (('Blue', blue, rig), ('Red', red, red_rig)):
        model.name = f'{label} | {side} | LOD0'
        armature.name = f'{label} | {side} | Formal skeleton'
        model['ModelId'] = source_row['id']
        model['FormalResource'] = source_row['mesh']
        model['Geometry and UV SHA256'] = original_digest
        model['A5 concept'] = f'{name}_A5.png'
        assert any(mod.type == 'ARMATURE' and mod.object == armature for mod in model.modifiers)
    mesh.name = f'{name} | Shared LOD0 with A5 finish slots'
    mesh.attributes.new('SSF Original Material Index', 'INT', 'FACE').data.foreach_set('value', original_classes)
    report = dict(model_id=source_row['id'], formal_resource=source_row['mesh'],
                  source_fbx=filepath.relative_to(ROOT).as_posix(),
                  source_fbx_sha256=hashlib.sha256(filepath.read_bytes()).hexdigest(),
                  geometry_uv_weights_sha256=original_digest, dims_m=dims, vertices=len(mesh.vertices),
                  triangles=len(mesh.polygons), bones=len(rig.data.bones),
                  shared_mesh=blue.data == red.data, assembly=[blue_collection.name, red_collection.name],
                  objects=[blue.name, red.name], material_slots=ids, by_part=assignments,
                  cooling_blades=dict(blades), major_outline=outline_report, main_panel_contours=contour_report)
    return (blue_collection, red_collection), (blue, red), report


def load_original_materials():
    material_names = ['SSF original outline shell', *(f'{name} - original display' for name, _ in NAMES)]
    with bpy.data.libraries.load(str(A4)) as (source, dest):
        assert all(name in source.materials for name in material_names), material_names
        dest.materials = list(material_names)
    outline = bpy.data.materials[material_names[0]]
    tree = outline.node_tree
    opacity = next(node for node in tree.nodes if node.type == 'MIX_SHADER').inputs[0]
    input_socket = opacity.links[0].from_socket
    control = tree.nodes.new('ShaderNodeValue')
    control.label = 'Scene Properties | SSF Stylization'
    multiplier = tree.nodes.new('ShaderNodeMath')
    multiplier.operation = 'MULTIPLY'
    tree.links.new(scene_style(control.outputs[0]), multiplier.inputs[0])
    tree.links.new(input_socket, multiplier.inputs[1])
    tree.links.new(multiplier.outputs[0], opacity)
    hidden_outline = bpy.data.materials.new('SSF suppressed outline faces')
    hidden_outline.surface_render_method = 'DITHERED'
    graph = Nodes(hidden_outline)
    transparent = graph.new('ShaderNodeBsdfTransparent')
    output = graph.new('ShaderNodeOutputMaterial')
    graph.put(output.inputs['Surface'], transparent.outputs[0])
    return outline, hidden_outline


def run(output_root, revision):
    output_root.mkdir(parents=True, exist_ok=True)
    output = output_root / f'SSF_A5_Materials_{revision}.blend'
    if output.exists():
        raise FileExistsError(output)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    overview = bpy.context.scene
    overview.name = '00 | SSF 六座总览'
    outline, hidden_outline = load_original_materials()
    source = json.loads((SOURCE / 'UE_LOD0.json').read_text(encoding='utf-8'))['models']
    concept = json.loads((REFERENCES / 'Reference_A5_Manifest.json').read_text(encoding='utf-8'))
    images = {row['key']: row for row in concept['deliverables']}
    ground = bpy.data.materials.new('Neutral matte review floor')
    ground.use_nodes = True
    shader = next(node for node in ground.node_tree.nodes if node.type == 'BSDF_PRINCIPLED')
    shader.inputs['Base Color'].default_value = (.58, .58, .60, 1.)
    shader.inputs['Roughness'].default_value = .87
    models = {}
    all_objects = []
    for i, (name, label) in enumerate(NAMES, 1):
        collections_, objects, report = import_and_paint(overview, name, label, source[name], outline, hidden_outline)
        bpy.context.view_layer.update()
        scene = bpy.data.scenes.new(f'{i:02} | {label}')
        for collection in collections_:
            attach_to_scene(scene, collection)
        image_path = REFERENCES / images[name]['file']
        assert hashlib.sha256(image_path.read_bytes()).hexdigest() == images[name]['sha256']
        image = bpy.data.images.load(str(image_path))
        image.pack()
        prepare_scene(scene, objects, ground, image)
        report.update(scene=scene.name, reference=image.name, reference_sha256=images[name]['sha256'])
        models[name] = report
        all_objects.extend(objects)
        print(f'{name}: {report["vertices"]} vertices, {report["bones"]} bones, {len(report["material_slots"])} finish slots', flush=True)

    prepare_scene(overview, all_objects, ground)
    bpy.context.window.scene = overview
    bpy.context.view_layer.update()
    assert all(scene[STYLE_PROPERTY] == 0. for scene in bpy.data.scenes)
    for model in all_objects:
        for slot in model.material_slots:
            material = slot.material
            if not material or 'Base sRGB' not in material:
                continue
            base = next(node for node in material.node_tree.nodes if node.label == 'Editable base color')
            assert np.allclose(tuple(base.outputs[0].default_value), linear_color(material['Base sRGB']), atol=1e-6)
            assert any(node.type == 'BSDF_PRINCIPLED' for node in material.node_tree.nodes)
    print('A5 color and physical material inputs verified before enabling line and three-tone pass', flush=True)
    for scene in bpy.data.scenes:
        scene[STYLE_PROPERTY] = 1.
    bpy.context.window.scene = overview
    bpy.context.view_layer.update()
    bpy.ops.object.select_all(action='DESELECT')
    bpy.context.view_layer.objects.active = None
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == 'VIEW_3D':
                area.spaces.active.region_3d.view_perspective = 'CAMERA'
                area.spaces.active.region_3d.view_camera_zoom = 7
                area.spaces.active.overlay.show_overlays = False
                area.spaces.active.shading.type = 'RENDERED'
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=True, relative_remap=False)
    report = {'project': output.name, 'stage': f'Blender material review {revision}', 'concept_stage': 'A5',
              'build_source': Path(os.path.relpath(ROOT, output_root)).as_posix(),
              'team_palette': TEAMS, 'reference_images_packed': len(images), 'models': models,
              'material_stage_verified_before_stylization': True, 'scene_style_property': STYLE_PROPERTY,
              'light_bands': list(LIGHT_BANDS), 'art_light_direction': list(ART_LIGHT)}
    (output_root / f'{revision}_Material_Manifest.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('SSF A5 BLENDER MATERIAL BUILD COMPLETE', flush=True)


if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if len(args) != 2 or not args[1].startswith('B') or not args[1][1:].isdigit():
        raise SystemExit('Supply -- <output directory> <revision such as B2>')
    run(Path(args[0]).resolve(), args[1])
