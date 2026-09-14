# Стингер, вторая доля (выполняется с задержкой — контекст игрока восстанавливаем через тег).
execute as @a[tag=kj_beat] at @s run playsound minecraft:block.note_block.bit hostile @s ~ ~ ~ 1 0.67
schedule function kojima:alert_beat2 5t
