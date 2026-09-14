# Стингер, третья доля + снятие тега.
execute as @a[tag=kj_beat] at @s run playsound minecraft:block.note_block.bit hostile @s ~ ~ ~ 1 0.5
tag @a[tag=kj_beat] remove kj_beat
