# Чистка всех эффектов пака: /trigger kj_go set 3.
effect clear @a[tag=kj_fly] minecraft:levitation
tag @a remove kj_fly
tag @a remove kj_beat
kill @e[type=minecraft:text_display,tag=kj_mark]
kill @e[type=minecraft:block_display,tag=kj_balloon]
tellraw @s {"text":"[KOJIMA] cleanup done","color":"gray"}
