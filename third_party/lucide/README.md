Lucide icons (https://lucide.dev), ISC license (see LICENSE).

`LucideIcons.cpp` embeds a subset of `lucide-static@1.50.0/font/lucide.ttf` with only the
icons listed in `LucideIcons.h`. To add icons: `npm pack lucide-static@1.50.0`, look up the
code points in `font/info.json`, subset with
`pyftsubset lucide.ttf --unicodes=<list> --no-hinting --drop-tables+=GSUB,GPOS,GDEF`
and regenerate both files (byte array + `ICON_*` UTF-8 macros).
