# Action items: meeting with Dr. Hassan, 24 Nov 2026

Only thesis items. (Fall detection was also discussed, but it is not part of the thesis.)

## Warda

1. **Check first whether the current models already fit on the Pi 5 and the Jetson Orin Nano.**
   - Check memory first, then computation (latency), before shrinking anything.
   - If they don't fit, shrinking for these boards is worth it. If they already fit, it may not be worth it for these two boards.
2. **Shrinking: start with quantization.**
   - For each quantized model, check memory first, then computation.
   - Look into other ways to shrink afterwards.
3. **Find out what the camera images are matched against: the 2D floorplan.** The input is camera images (already answered in the meeting). The open question is the floorplan itself: is it an image, or some other kind of data? Dr. Hassan mentioned that some methods use laser.
4. **Find out the output representation: in what form it is stored.**

## Shams

- *(to be added)*
