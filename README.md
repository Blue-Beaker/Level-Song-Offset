# LevelSongOffset

Level-specific song offset changer.  

Also includes a workaround for negative audio offset:  
Negative audio offset doesn't always work on GD. Sometimes it just doesn't work and breaks music after respawning from the start.  
This mod fixes it by adding an empty space at the start of the music, replacing the original one.  

Known incompatibilities:
- Jukebox - while compatible, when negative offset workaround is active, after selecting a NONG in jukebox, you need to delete cached music in this mod manually, for the selected NONG to take effect.  

<img src="logo.png" width="150" alt="the mod's logo" />

*Update logo.png to change your mod's icon (please)*

## Build instructions
Just follow geode instructions. The scripts in `scripts` folder is for building for windows on linux.
[geode docs](https://docs.geode-sdk.org/getting-started/create-mod#build)
```sh
# Assuming you have the Geode CLI set up already
geode build
```