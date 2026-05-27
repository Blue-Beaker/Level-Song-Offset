# LevelSongOffset

Level-specific song offset changer.  

Also includes a workaround for negative audio offset:  
Negative audio offset doesn't always work on GD. Sometimes it just doesn't work and breaks music after respawning from the start.  
This mod fixes it by adding an empty space at the start of the music, replacing the original one.  

Compatible with jukebox's NONG. negative offset workaround cache for different NONGs are created separately.

<img src="logo.png" width="150" alt="the mod's logo" />

*Update logo.png to change your mod's icon (please)*

## Build instructions
Just follow geode instructions. The scripts in `scripts` folder is for building for windows on linux.
[geode docs](https://docs.geode-sdk.org/getting-started/create-mod#build)
```sh
# Assuming you have the Geode CLI set up already
geode build
```