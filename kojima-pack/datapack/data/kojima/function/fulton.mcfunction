# FULTON RECOVERY: шар уносит игрока в небо, в конце — плавное снижение.
# Запускать as/at игрок: /trigger kj_go set 1 или /function kojima:fulton.
tag @s add kj_fly
effect give @s minecraft:levitation 4 8 true
execute at @s run summon minecraft:block_display ~ ~ ~ {Tags:["kj_balloon"],block_state:{Name:"minecraft:white_wool"},brightness:{sky:15,block:15},transformation:{left_rotation:[0f,0f,0f,1f],right_rotation:[0f,0f,0f,1f],translation:[-0.6f,2.3f,-0.6f],scale:[1.2f,1.5f,1.2f]}}
ride @n[type=minecraft:block_display,tag=kj_balloon,distance=..8] mount @s
playsound minecraft:entity.firework_rocket.launch player @s ~ ~ ~ 1 1
playsound minecraft:entity.pufferfish.blow_up player @s ~ ~ ~ 0.8 0.7
schedule function kojima:fulton_end 4s
