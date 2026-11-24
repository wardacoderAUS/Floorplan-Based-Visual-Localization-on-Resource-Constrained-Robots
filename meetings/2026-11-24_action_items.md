# Action items: meeting with Dr. Hassan, 24 Nov 2026

Only thesis items. (Fall detection was also discussed, but it is not part of the thesis.)

## Warda

1. **Check first whether the current models already fit on the Pi 5 and the Jetson Orin Nano.**
   - Check memory first, then computation (latency), before shrinking anything.
   - If they don't fit, shrinking for these boards is worth it. If they already fit, it may not be worth it for these two boards.
2. **Shrinking: start with quantization.**
   - For each quantized model, check memory first, then computation.
   - Look into other ways to shrink afterwards.
3. **Find out how the 2D floorplan is represented.** Is it stored as an image, or as some other kind of data?
4. **Find out what the "output representation" is.** Dr. Hassan's term; I didn't fully understand the question in the meeting, so I need to look into it.
5. **Explain what the localization uses as input.** Is it visual (camera images) or laser? Dr. Hassan mentioned that some methods use laser.

## Shams

- *(to be added)*
