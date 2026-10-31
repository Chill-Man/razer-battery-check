# Mouse image

`basilisk-cutout.png` — прозрачный вырезанный вариант предоставленной пользователем фотографии `71L-flqtTwL.jpg`. Обработан встроенным инструментом imagegen (background-extraction). `src/mouse.png` — уменьшенный ресурс для приложения с сохранённым альфа-каналом; подготавливается `prepare-mouse-asset.ps1`.

Финальный промпт:

> Use case: background-extraction. Edit target: the provided product photograph of a Razer Basilisk V3 Pro mouse. Produce a precise clean cutout of ONLY this exact mouse, with a real transparent alpha background. Remove all white background AND all multicolored exterior glow/halo/shadow outside the physical silhouette. Preserve the mouse's exact top view, proportions, orientation, all body details, textured grips, buttons, green wheel lighting, and green Razer snake logo. Do not redraw, stylize, rotate, add a cable, or change the product. Center the full uncut mouse with a small transparent margin, ready to embed as an image asset in a dark desktop app. No text or extra objects.
