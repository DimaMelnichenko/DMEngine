# Тестовая модель для конвертера glTF (Tools/import_gltf.py). Запускается внутри Blender без окна:
#   blender -b --factory-startup --python Tools/blender_test_model.py -- <выход.glb>
# Сцена:
#   TestRock, TestRock_LOD1 — искажённая икосфера и её упрощённая копия (LOD), базовый цвет — сетка Blender
#     «Color Grid» с буквами и цифрами (по ним видно зеркалирование развёртки), шероховатость 0.8;
#   TestPanel — плита лицом к виду спереди (−Y): спереди Color Grid и карта нормалей с полусферами-выпуклостями
#     (выпуклости должны освещаться со стороны света), торцы — второй материал «Frame»: золото, metallic 1,
#     roughness 0.3 (объект с двумя материалами конвертер делит на две модели).
# Экспорт — как советует docs/models.md: GLB, +Y вверх, касательные, модификаторы применены.
# Сообщения скрипта — ASCII (правило Tools/).
import os
import sys

import bpy
import numpy as np


def output_path():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if not argv:
        raise SystemExit('usage: blender -b --factory-startup --python Tools/blender_test_model.py -- <out.glb>')
    return os.path.abspath(argv[0])


def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)


def save_image(image, folder):
    # Сгенерированную картинку сохраняем в PNG: экспортёр glTF берёт пиксели из файла
    image.filepath_raw = os.path.join(folder, image.name + '.png')
    image.file_format = 'PNG'
    image.save()


def color_grid(name, folder):
    image = bpy.data.images.new(name, 1024, 1024)
    image.generated_type = 'COLOR_GRID'
    save_image(image, folder)
    return image


def bump_normal_map(name, folder, size=256, cells=4):
    # Полусферы в касательном пространстве, соглашение OpenGL/glTF: зелёный — вверх по картинке.
    # Пиксели Blender идут снизу вверх, поэтому y здесь растёт вверх
    t = (np.arange(size) + 0.5) / size * cells
    fx, fy = np.meshgrid(t % 1.0 * 2.0 - 1.0, t % 1.0 * 2.0 - 1.0)
    radius = 0.8
    dx, dy = fx / radius, fy / radius
    inside = dx * dx + dy * dy < 1.0
    nz = np.sqrt(np.clip(1.0 - dx * dx - dy * dy, 0.0, 1.0))
    normal = np.where(inside[..., None], np.stack([dx, dy, nz], -1), np.array([0.0, 0.0, 1.0]))
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    rgba = np.concatenate([normal * 0.5 + 0.5, np.ones((size, size, 1))], -1).astype(np.float32)

    image = bpy.data.images.new(name, size, size)
    image.colorspace_settings.name = 'Non-Color'
    image.pixels.foreach_set(rgba.ravel())
    save_image(image, folder)
    return image


def material(name, base_color=(1.0, 1.0, 1.0), metallic=0.0, roughness=0.5, color_image=None, normal_image=None,
             engine_params=None):
    """engine_params — параметры движка, которых нет в glTF: Custom Properties → extras → экземпляр материала"""
    mat = bpy.data.materials.new(name)
    for key, value in (engine_params or {}).items():
        mat[key] = value
    if bpy.app.version < (5, 0, 0):  # в Blender 5 узлы материала включены всегда
        mat.use_nodes = True
    mat.use_backface_culling = True   # меши замкнутые: задние грани не нужны (doubleSided = false в glTF)
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes['Principled BSDF']
    bsdf.inputs['Base Color'].default_value = (*base_color, 1.0)
    bsdf.inputs['Metallic'].default_value = metallic
    bsdf.inputs['Roughness'].default_value = roughness
    if color_image:
        tex = nodes.new('ShaderNodeTexImage')
        tex.image = color_image
        links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    if normal_image:
        tex = nodes.new('ShaderNodeTexImage')
        tex.image = normal_image
        normal_map = nodes.new('ShaderNodeNormalMap')
        links.new(tex.outputs['Color'], normal_map.inputs['Color'])
        links.new(normal_map.outputs['Normal'], bsdf.inputs['Normal'])
    return mat


def make_rock(folder):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=4, radius=1.0, location=(0.0, 0.0, 1.0))
    rock = bpy.context.active_object
    rock.name = 'TestRock'
    # Смена LOD дизерингом: на 25 м видна смесь двух сеток (docs/models.md)
    rock.data.materials.append(material('RockMat', roughness=0.8, color_image=color_grid('RockColor', folder),
                                        engine_params={'DitheredLODTransition': 'true'}))
    for polygon in rock.data.polygons:
        polygon.use_smooth = True

    noise = bpy.data.textures.new('RockNoise', 'CLOUDS')
    noise.noise_scale = 0.6
    displace = rock.modifiers.new('Displace', 'DISPLACE')
    displace.texture = noise
    displace.strength = 0.35

    # LOD1 — копия с тем же мешем и материалом, упрощённая модификатором; стоит в стороне, как обычно в Blender
    lod1 = rock.copy()
    lod1.name = 'TestRock_LOD1'
    lod1.location = (-3.0, 0.0, 1.0)
    bpy.context.collection.objects.link(lod1)
    decimate = lod1.modifiers.new('Decimate', 'DECIMATE')
    decimate.ratio = 0.2


def make_panel(folder):
    # Плоскость в XY лицом к +Z, повёрнутая на 90° вокруг X, смотрит в −Y (вид спереди), текстура стоит прямо
    bpy.ops.mesh.primitive_plane_add(size=2.0, location=(3.0, 0.0, 1.2), rotation=(np.pi / 2.0, 0.0, 0.0))
    panel = bpy.context.active_object
    panel.name = 'TestPanel'
    panel.data.materials.append(material('PanelFront', roughness=0.5, color_image=color_grid('PanelColor', folder),
                                         normal_image=bump_normal_map('PanelNormal', folder)))
    panel.data.materials.append(material('Frame', base_color=(1.0, 0.77, 0.33), metallic=1.0, roughness=0.3))

    solidify = panel.modifiers.new('Solidify', 'SOLIDIFY')
    solidify.thickness = 0.15
    solidify.offset = -1.0           # толщина против нормали — назад, лицевая сторона остаётся на месте
    solidify.use_rim = True
    solidify.material_offset_rim = 1  # торцы — материал Frame


def main():
    out = output_path()
    folder = os.path.splitext(out)[0] + '_images'
    os.makedirs(folder, exist_ok=True)

    clear_scene()
    make_rock(folder)
    make_panel(folder)

    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=True, export_apply=True,
                              export_tangents=True, export_image_format='AUTO', export_materials='EXPORT',
                              export_extras=True)
    print('Written: ' + out)


main()
