# MGS ALERT: красный "!" над головой с pop-анимацией + стингер на нотных блоках.
# Запускать as/at игрок (см. tick.mcfunction).
tag @s add kj_beat
execute at @s run summon minecraft:text_display ~ ~2.4 ~ {Tags:["kj_mark"],billboard:"vertical",alignment:"center",see_through:1b,background:0,shadow:1b,brightness:{sky:15,block:15},text:'{"text":"!","bold":true,"color":"red"}',transformation:{left_rotation:[0f,0f,0f,1f],right_rotation:[0f,0f,0f,1f],translation:[0f,0f,0f],scale:[0.05f,0.05f,0.05f]},interpolation_duration:5,start_interpolation:0}
schedule function kojima:alert_pop 1t
schedule function kojima:alert_end 2s
playsound minecraft:block.note_block.bit hostile @s ~ ~ ~ 1 0.5
playsound minecraft:block.note_block.snare hostile @s ~ ~ ~ 0.6 1.3
schedule function kojima:alert_beat 6t
