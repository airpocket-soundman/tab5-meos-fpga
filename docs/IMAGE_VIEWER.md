# BMP image viewer

`image /sd/example.bmp` opens a BMP on the Tab5 LCD. `image --info /sd/example.bmp` reports dimensions, decoded size and pixel checksum without opening the viewer. Relative paths are supported.

Q, Escape, Enter or Ctrl-C closes the viewer. Z or Space toggles enlargement. Aspect ratio is preserved. The existing BMP decoder accepts supported 16/24-bit BMP variants up to 1920x1080; JPEG and PNG are not implemented. The viewer allocates width × height × 2 bytes for decoded RGB565 pixels, plus one output row (2528 bytes for the current 1264-pixel viewport). The existing Filer viewer is unchanged.

Host tests cover real BMP decoding, malformed/short input, scaled pixel values, geometry, zoom, cancellation and allocation/presentation failure cleanup. On Tab5, a 2x2 BMP uploaded through the shell passed metadata/checksum checks, viewer start/close and missing-file rejection. Internal and PSRAM usage returned exactly to their prior values; display counters reported zero errors/timeouts and all submitted batches retired. Physical visual inspection and large-image/storage-specific runtime tests remain pending.

Evidence: [device result](validation/image-hardware-result.json). Run `python tests/test_image.py --cc PATH_TO_COMPILER` for host tests.
