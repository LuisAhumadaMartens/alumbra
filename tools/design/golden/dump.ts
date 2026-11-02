import {
  contexts,
  defaultContext,
  resolveTokens,
  tokenNames,
  tokenTypes,
  type Color,
  type Context,
  type CubicBezier,
  type Overrides,
  type ShadowLayer,
  type Tokens,
} from "../../../antumbra/generated/DesignTokens.ts";

type Json = number | string | boolean | Json[] | { [key: string]: Json };

function color(value: Color): Json {
  return {
    colorSpace: value.colorSpace,
    components: [value.components.red, value.components.green, value.components.blue],
    alpha: value.alpha,
    srgb: [value.srgb.red, value.srgb.green, value.srgb.blue],
  };
}

function canonical(tokens: Tokens, key: keyof Tokens): Json {
  const value: unknown = tokens[key];
  switch (tokenTypes[key]) {
    case "color":
      return color(value as Color);
    case "cubicBezier": {
      const curve = value as CubicBezier;
      return [curve.x1, curve.y1, curve.x2, curve.y2];
    }
    case "fontFamily":
      return [...(value as readonly string[])];
    case "shadow":
      return (value as readonly ShadowLayer[]).map((layer) => ({
        color: color(layer.color),
        offsetX: layer.offsetX,
        offsetY: layer.offsetY,
        blur: layer.blur,
        spread: layer.spread,
        inset: layer.inset,
      }));
    default:
      return value as number;
  }
}

function dump(tokens: Tokens): Json {
  const result: { [key: string]: Json } = {};
  for (const key of Object.keys(tokenNames) as (keyof Tokens)[]) {
    result[tokenNames[key]] = canonical(tokens, key);
  }
  return result;
}

function selections(): Context[] {
  let result: Context[] = [defaultContext];
  for (const modifier of Object.keys(contexts) as (keyof Context)[]) {
    const next: Context[] = [];
    for (const selection of result) {
      for (const option of contexts[modifier]) {
        next.push({ ...selection, [modifier]: option });
      }
    }
    result = next;
  }
  return result;
}

function srgb(red: number, green: number, blue: number, alpha: number): Color {
  const components = { red, green, blue };
  return { colorSpace: "srgb", components, alpha, srgb: components };
}

const output: { [key: string]: Json } = {};
for (const selection of selections()) {
  const key = (Object.keys(contexts) as (keyof Context)[])
    .map((modifier) => selection[modifier])
    .join("/");
  output[key] = dump(resolveTokens(selection));
}

const semantic: Overrides = {
  alumbra_token_color_scontrol_sbackground: srgb(0.25, 0.5, 0.75, 1),
};
const component: Overrides = {
  ...semantic,
  alumbra_token_player_sbutton_sbackground_sfocus: srgb(0.125, 0.25, 0.375, 0.5),
};

console.log(
  JSON.stringify({
    language: "typescript",
    contexts: output,
    scenarios: {
      semantic: dump(resolveTokens(defaultContext, semantic)),
      component: dump(resolveTokens(defaultContext, component)),
    },
  }),
);
