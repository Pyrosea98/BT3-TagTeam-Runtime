<!-- bt3-revision: roster-power-fusion-20261110 -->

# 0.5.alpha.1 — Roster, tournaments, power scaling and fusion fixes

New experimental **PC Nightly update**. The previous **0.5.alpha** release and stable **0.4.1** remain available and unchanged. Android is being maintained separately by Claude; this release contains no APK.

## Downloads
- **Already installed:** use the launcher's **Nightly** update channel / Check for updates. The download and installation require your agreement. Alternatively download **Windows-Update-Setup.exe**, close the game, and select the existing 0.4.1 or 0.5.alpha installation. **No ISO re-extraction** for a supported installation.
- **New players:** use **Windows-Setup.exe** and supply your supported Power Scale BETA 8 / compatible R4 ISO on first launch.
- **Windows-Update.zip** is the portable update alternative; you do not need it if using Update Setup or the built-in updater.
- Saves, earned trophies, controller bindings, settings and **normal / optional 2× map choice** are preserved. No ISO, BIOS, personal save or diagnostic fight checkpoint is bundled.

## Included since 0.5.alpha
- Tag Team Tournament startup, selection/confirmation and round presentation corrections; individual ring-outs eliminate that fighter. Per-tournament backgrounds and the imported Other World arena; Yamcha tournament draws valid random characters, forms and costumes.
- Roster and costume-specific transformation audit: Daima children, Scouter Vegeta, Future Gohan, Sword Trunks, Cold's actual form order, Oceanus and Tamagamis, and independent character promotions. Shorty, Scarface and Cacao have separate entries; the Pendulum training chapter sits between the existing training and Saiyan-arrival chapters.
- Portrait/chip coverage and original-art upscaling, including overhead portraits; accepted Vegetto GT SSJ2 fringe/material correction. No rejected generative portrait repaint is included.
- Gogetto SSJ4 powers follow the approved progression: **232T Metamoran / 464T Potara**, with actual melee/technique/gradual-defence values aligned.
- Fat Buu no longer follows reserved Duu/Multiverse costume routes; Duu no longer inherits unrelated Super Buu transformations.
- Fixed a cancelled-character-preview buffer ownership bug that could cause a double-free while browsing rapidly.
- **5× power gap:** ordinary physical hits still deal damage but do not stagger the sufficiently stronger defender. Grabs, throws, beams and techniques retain their normal reactions. Local modes only; existing Story-specific rules remain.
- Giant body-separation radius follows its scaled size; swept separation also checks non-target giants to reduce pass-through. Existing cinematic/paired-action exclusions remain.
- Goku GT SSJ4's **neutral R3 → Full Power** default restored. Goku can initiate the existing GT Dance/Potara recipes, including Full Power; modern-selector P1/P2 partner lookup corrected. Native checks loaded **Gogeta SSJ4** and **Vegetto GT SSJ4** with Goku initiating.
- Fusion-consumed P1/P2 partners no longer receive a revival ring. Genuine fallen teammates retain revival indicators.

## Known limits
This is an alpha, not a claim that every roster/cinematic branch has been manually replayed. **Supreme Kai Planet's water-square artifacts remain unresolved on PC and Android**, particularly in reported enlarged-map cases. Namek remains coming soon; no new playable Namek saga or APK is included. Original imported artwork/voices can retain their source language. A different ISO revision requires a compatible import recipe.

## Español
Nueva actualización de prueba **0.5.alpha.1 para Nightly**. La 0.5.alpha anterior y la estable 0.4.1 no se sustituyen. Si ya tienes el juego, busca actualizaciones en Nightly o usa **Windows-Update-Setup.exe** sobre tu instalación: conserva partidas, logros, controles, ajustes y mapas normales/2×, sin volver a extraer una ISO compatible. Para empezar desde cero usa **Windows-Setup.exe** y tu ISO compatible.

Incluye correcciones de torneos, personajes, trajes y transformaciones; retratos ampliados desde el arte original; los nuevos saiyans y Cacao; progresión de poder de Gogetto; navegación de retratos sin el doble borrado; resistencia a golpes físicos con una diferencia de poder de 5×; colisión de gigantes; R3 de Goku SSJ4 y sus fusiones GT; y el anillo de reanimación que aparecía tras fusionarse. **Los cuadros de agua del Planeta Supremo siguen pendientes.** El APK lo prepara Claude por separado.

## Credits / Créditos
Juan: project direction, live testing and integration decisions. Original Tag Team work: **tehmufti**. Power Scale base: **LetsPlayBt3**. Decompilation/native engine: **z3xox/Tenkaichi3Decomp** and upstream contributors. **RidJuampa** helped Juan understand the code: https://www.youtube.com/@RidJuampa. Thanks to all modders and asset authors named in the in-game credits; those credits remain included. AI-assisted development: Codex, Claude, DeepSeek and Qwen. Source snapshots and SHA256 checksums accompany the downloads.
