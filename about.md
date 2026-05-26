# Level Song Offset

Level-specific song offset changer.  

Also includes a workaround for negative audio offset:  
Negative audio offset doesn't always work on GD. Sometimes it just doesn't work and breaks music after respawning from the start.  
This mod fixes it by adding an empty space at the start of the music, replacing the original one.  

Known incompatibilities:
- Jukebox - while compatible, when negative offset workaround is active, after selecting a NONG in jukebox, you need to delete cached music in this mod manually, for the selected NONG to take effect.  