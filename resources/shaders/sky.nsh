@sampler dith_map "dithering"
@predefined vp "nya viewport"

@all
varying vec4 color;
varying vec3 tc;

@vertex
uniform vec4 vp;

void main()
{
    vec4 pos = gl_ModelViewProjectionMatrix * gl_Vertex;
    tc.xy = vp.zw / vec2(64.0) * 0.5 * pos.xy;
    tc.z = pos.w;
    color = gl_Color;

    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    //far-plane depth clamp: keeps sky-sphere vertices inside the clip range (z<w)
    //on GPUs that round z/w to exactly 1.0 at w~60000 (Apple Silicon discarded
    //whole triangles at the original 2e-7, showing as black screen-fixed patches).
    //1e-5 gives ample ULP margin. Terrain-overdraw safety no longer depends on
    //this value: the sky is drawn FIRST in location::draw() and never writes
    //depth, so every later draw pass overwrites it.
    gl_Position.z = min(gl_Position.z / gl_Position.w, 1.0 - 1e-5) * gl_Position.w;
}

@fragment
uniform sampler2D dith_map;

void main()
{
    vec4 dith = texture2D(dith_map, tc.xy / tc.z);
    gl_FragColor = (color + dith*0.015) * 0.8;
}
