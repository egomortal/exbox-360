"""Project the source concept onto visible reconstructed mesh surfaces."""
import sys
import numpy as np
import trimesh
import moderngl
from PIL import Image

sys.path.insert(0, sys.argv[5])
from tsr.bake_texture import rasterize_position_atlas
from tsr.utils import resize_foreground

mesh = trimesh.load(sys.argv[1], process=False)
original = np.asarray(mesh.vertices).copy()
scale = 1.73 / (original[:,2].max()-original[:,2].min())
mid_y = (original[:,1].max()+original[:,1].min())/2
mid_x = (original[:,0].max()+original[:,0].min())/2
mesh.vertices = np.column_stack(((original[:,1]-mid_y)*scale,
                                 (original[:,2]-original[:,2].min())*scale,
                                 (original[:,0]-mid_x)*scale))
base = np.asarray(Image.open(sys.argv[2]).convert("RGBA")).copy()
reference = np.asarray(resize_foreground(Image.open(sys.argv[3]).convert("RGBA"), .85))
height, width = base.shape[:2]
if width != height:
    raise ValueError("square texture expected")
position = rasterize_position_atlas(
    mesh, np.arange(len(mesh.vertices), dtype=np.int32),
    np.asarray(mesh.faces, dtype=np.int32), np.asarray(mesh.visual.uv, dtype=np.float32),
    width, 0)
position = np.flipud(position)

# Render a front-facing z-buffer at the same projection used for the source.
verts = np.asarray(mesh.vertices, dtype=np.float32)
xmin, ymin = verts[:, :2].min(axis=0)
xmax, ymax = verts[:, :2].max(axis=0)
ctx = moderngl.create_context(standalone=True)
ctx.enable(moderngl.DEPTH_TEST)
program = ctx.program(
    vertex_shader="""#version 330
    in vec3 in_pos;
    out vec3 v_pos;
    uniform vec4 bounds;
    void main() {
        v_pos = in_pos;
        float x = (in_pos.x-bounds.x)/(bounds.z-bounds.x)*2.0-1.0;
        float y = (in_pos.y-bounds.y)/(bounds.w-bounds.y)*2.0-1.0;
        gl_Position = vec4(x,y,-in_pos.z*0.7,1.0);
    }""",
    fragment_shader="""#version 330
    in vec3 v_pos;
    out vec4 o_col;
    void main() { o_col=vec4(v_pos,1.0); }""")
program["bounds"].value = (float(xmin),float(ymin),float(xmax),float(ymax))
vbo = ctx.buffer(verts.tobytes())
ibo = ctx.buffer(np.asarray(mesh.faces,dtype=np.int32).tobytes())
vao = ctx.vertex_array(program,[(vbo,"3f","in_pos")],ibo)
color = ctx.texture((width,width),4,dtype="f4")
depth = ctx.depth_renderbuffer((width,width))
fbo = ctx.framebuffer(color_attachments=[color],depth_attachment=depth)
fbo.use(); fbo.clear(0,0,0,0,depth=1)
vao.render()
front = np.flipud(np.frombuffer(color.read(),dtype=np.float32).reshape(width,width,4))

valid = position[:,:,3] > .5
px,py,pz = position[:,:,0],position[:,:,1],position[:,:,2]
ix = np.clip(((px-xmin)/(xmax-xmin)*(width-1)).astype(np.int32),0,width-1)
iy = np.clip(((ymax-py)/(ymax-ymin)*(width-1)).astype(np.int32),0,width-1)
front_z = front[iy,ix,2]
visible = valid & (front[iy,ix,3]>.5) & (pz >= front_z-.06)

alpha = reference[:,:,3] > 100
ys,xs = np.where(alpha)
left,right,top,bottom = xs.min(),xs.max(),ys.min(),ys.max()
u = left + (px-xmin)/(xmax-xmin)*(right-left)
v = top + (ymax-py)/(ymax-ymin)*(bottom-top)
ru = np.clip(np.rint(u).astype(np.int32),0,reference.shape[1]-1)
rv = np.clip(np.rint(v).astype(np.int32),0,reference.shape[0]-1)
source = reference[rv,ru]
visible &= source[:,:,3] > 180
face_override = valid & (py > 1.20) & (pz > .045) & (source[:,:,3] > 180)
visible |= face_override
print("face override", face_override.sum())
base[visible,:3] = (source[visible,:3].astype(np.float32)*.98
                    + base[visible,:3].astype(np.float32)*.02).astype(np.uint8)
Image.fromarray(base).save(sys.argv[4])
print(f"Projected concept onto {visible.sum():,} of {valid.sum():,} texture pixels")
