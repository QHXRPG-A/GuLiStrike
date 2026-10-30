/** sRGB height ramp shared by the 2D raster, legend and 3D vertex colors. */
export const HEIGHT_COLOR_MIN_CM = -50_000;
export const HEIGHT_COLOR_MAX_CM = 50_000;
export const HEIGHT_COLOR_STOPS = ['#274E61', '#0D9099', '#557B78', '#D5C09C', '#EE9D58', '#8E3A2A'] as const;
const stops = HEIGHT_COLOR_STOPS.map(hex => [1, 3, 5].map(offset => parseInt(hex.slice(offset, offset + 2), 16)));

export function heightColor(height: number): [number, number, number] {
  const normalized = Math.max(0, Math.min(1, (height - HEIGHT_COLOR_MIN_CM) / (HEIGHT_COLOR_MAX_CM - HEIGHT_COLOR_MIN_CM)));
  const position = normalized * (stops.length - 1), index = Math.min(stops.length - 2, Math.floor(position));
  const fraction = position - index, a = stops[index], b = stops[index + 1];
  return [a[0] + (b[0] - a[0]) * fraction, a[1] + (b[1] - a[1]) * fraction, a[2] + (b[2] - a[2]) * fraction];
}
