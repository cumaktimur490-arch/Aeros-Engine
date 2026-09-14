# Конец фултона: шар лопается, игрок плавно снижается (slow falling 8с).
execute as @e[type=minecraft:block_display,tag=kj_balloon] at @s run particle minecraft:poof ~ ~1 ~ 0.5 0.5 0.5 0.05 12
execute as @a[tag=kj_fly] at @s run playsound minecraft:entity.firework_rocket.blast player @s ~ ~ ~ 0.8 1.2
effect give @a[tag=kj_fly] minecraft:slow_falling 8 0 true
effect clear @a[tag=kj_fly] minecraft:levitation
tag @a[tag=kj_fly] remove kj_fly
kill @e[type=minecraft:block_display,tag=kj_balloon]
