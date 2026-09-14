# KOJIMA PACK — выполняется каждый тик (см. tick.json).
# Инициализация новичков + тикающий кулдаун.
scoreboard players add @a kj_cd 0
scoreboard players enable @a kj_go
scoreboard players remove @a[scores={kj_cd=1..}] kj_cd 1
# Получил урон = засекли: MGS-алерт (кулдаун 3 секунды).
execute as @a[scores={kj_dmg=1..,kj_cd=..0}] at @s run function kojima:alert
scoreboard players set @a[scores={kj_dmg=1..}] kj_cd 60
scoreboard players reset @a[scores={kj_dmg=1..}] kj_dmg
# Ручные триггеры (команда доступна без прав OP):
# /trigger kj_go set 1 — фултон, set 2 — тест алерта, set 3 — чистка.
execute as @a[scores={kj_go=1}] at @s run function kojima:fulton
execute as @a[scores={kj_go=2}] at @s run function kojima:alert
execute as @a[scores={kj_go=3..}] run function kojima:cleanup
scoreboard players reset @a[scores={kj_go=1..}] kj_go
# Облачка под летящими на фултоне.
execute as @a[tag=kj_fly] at @s run particle minecraft:cloud ~ ~0.3 ~ 0.4 0.2 0.4 0.02 2
