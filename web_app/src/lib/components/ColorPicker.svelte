<script lang="ts">
  export interface RGBWColor {
    r: number; // 0-255
    g: number;
    b: number;
    w: number;
    flicker?: boolean;
  }

  interface Props {
    value?: RGBWColor;
    onchange?: (color: RGBWColor) => void;
  }

  let { value = $bindable({ r: 255, g: 0, b: 0, w: 0, flicker: false }), onchange }: Props = $props();

  let r = $state(value.r);
  let g = $state(value.g);
  let b = $state(value.b);
  let w = $state(value.w);
  let flicker = $state(value.flicker ?? false);

  /* Sync internal state → bound value whenever any channel changes. */
  $effect(() => {
    const v = { r, g, b, w, flicker };
    value = v;
    onchange?.(v);
  });

  /* RGB preview blends the colour with white via the W channel. */
  let previewStyle = $derived(() => {
    const alpha = w / 255;
    const blend = (ch: number) => Math.round(ch + (255 - ch) * alpha);
    return `background: rgb(${blend(r)}, ${blend(g)}, ${blend(b)})`;
  });

  let hex = $derived(
    '#' + [r, g, b].map(v => v.toString(16).padStart(2, '0')).join('')
  );

  function clamp(n: number) {
    return Math.max(0, Math.min(255, Math.round(n)));
  }

  function onHexInput(e: Event) {
    const raw = (e.target as HTMLInputElement).value.replace(/^#/, '');
    if (raw.length !== 6) return;
    const parsed = parseInt(raw, 16);
    if (isNaN(parsed)) return;
    r = clamp((parsed >> 16) & 0xff);
    g = clamp((parsed >> 8)  & 0xff);
    b = clamp(parsed         & 0xff);
  }

  const CHANNELS = [
    { label: 'R', key: 'r' as const, color: '#e05555' },
    { label: 'G', key: 'g' as const, color: '#50c878' },
    { label: 'B', key: 'b' as const, color: '#5599f5' },
    { label: 'W', key: 'w' as const, color: '#cccccc' },
  ] as const;

  function setValue(key: 'r' | 'g' | 'b' | 'w', val: number) {
    const clamped = clamp(val);
    if (key === 'r') r = clamped;
    else if (key === 'g') g = clamped;
    else if (key === 'b') b = clamped;
    else if (key === 'w') w = clamped;
  }

  function getVal(key: 'r' | 'g' | 'b' | 'w') {
    if (key === 'r') return r;
    if (key === 'g') return g;
    if (key === 'b') return b;
    return w;
  }
</script>

<div class="picker">
  <!-- Preview swatch -->
  <div class="preview" style={previewStyle()}>
    <input
      type="text"
      class="hex-input"
      value={hex}
      maxlength="7"
      spellcheck="false"
      oninput={onHexInput}
      aria-label="Hex colour"
    />
  </div>

  <!-- RGBW sliders -->
  <div class="sliders">
    {#each CHANNELS as ch}
      {@const val = getVal(ch.key)}
      <div class="row">
        <span class="label" style="color: {ch.color}">{ch.label}</span>
        <input
          type="range"
          min="0"
          max="255"
          value={val}
          oninput={(e) => setValue(ch.key, Number((e.target as HTMLInputElement).value))}
          style="--track: {ch.color}"
          aria-label="{ch.label} channel"
        />
        <input
          type="number"
          min="0"
          max="255"
          value={val}
          oninput={(e) => setValue(ch.key, Number((e.target as HTMLInputElement).value))}
          aria-label="{ch.label} value"
        />
      </div>
    {/each}
  </div>

  <!-- Flicker toggle -->
  <label class="flicker-label">
    <input
      type="checkbox"
      bind:checked={flicker}
      aria-label="Enable candlelight flicker"
    />
    <span>Candlelight Flicker</span>
  </label>
</div>

<style>
  .picker {
    display: inline-flex;
    flex-direction: column;
    gap: 0.75rem;
    width: 280px;
    background: #1a1a1a;
    border: 1px solid #2e2e2e;
    border-radius: 8px;
    padding: 1rem;
    font-family: system-ui, sans-serif;
    font-size: 0.875rem;
  }

  /* ── preview ── */
  .preview {
    height: 64px;
    border-radius: 6px;
    display: flex;
    align-items: flex-end;
    justify-content: flex-end;
    padding: 0.4rem;
    transition: background 0.1s;
  }

  .hex-input {
    width: 7ch;
    padding: 0.2rem 0.4rem;
    background: rgba(0, 0, 0, 0.5);
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 4px;
    color: #fff;
    font-family: monospace;
    font-size: 0.8rem;
    text-align: center;
  }

  .hex-input:focus {
    outline: none;
    border-color: rgba(255, 255, 255, 0.4);
  }

  /* ── sliders ── */
  .sliders {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .row {
    display: grid;
    grid-template-columns: 1.2rem 1fr 3.5rem;
    align-items: center;
    gap: 0.6rem;
  }

  .label {
    font-weight: 700;
    font-size: 0.8rem;
    text-align: center;
  }

  input[type='range'] {
    -webkit-appearance: none;
    -moz-appearance: none;
    appearance: none;
    width: 100%;
    height: 6px;
    border-radius: 3px;
    background: #2e2e2e;
    outline: none;
    cursor: pointer;
  }

  input[type='range']::-webkit-slider-thumb {
    -webkit-appearance: none;
    appearance: none;
    width: 14px;
    height: 14px;
    border-radius: 50%;
    background: var(--track, #fff);
    border: 2px solid #111;
    cursor: pointer;
  }

  input[type='range']::-moz-range-thumb {
    width: 14px;
    height: 14px;
    border-radius: 50%;
    background: var(--track, #fff);
    border: 2px solid #111;
    cursor: pointer;
  }

  input[type='number'] {
    width: 100%;
    padding: 0.25rem 0.3rem;
    background: #111;
    border: 1px solid #2e2e2e;
    border-radius: 4px;
    color: inherit;
    font-size: 0.8rem;
    text-align: right;
    -moz-appearance: textfield;
    appearance: textfield;
  }

  input[type='number']::-webkit-outer-spin-button,
  input[type='number']::-webkit-inner-spin-button {
    -webkit-appearance: none;
  }

  input[type='number']:focus {
    outline: none;
    border-color: #444;
  }

  .flicker-label {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    cursor: pointer;
    font-size: 0.85rem;
    color: #ccc;
    padding: 0.5rem 0;
    border-top: 1px solid #2e2e2e;
    margin-top: 0.5rem;
  }

  .flicker-label input[type='checkbox'] {
    cursor: pointer;
    width: 16px;
    height: 16px;
  }

  .flicker-label:hover {
    color: #fff;
  }
</style>
