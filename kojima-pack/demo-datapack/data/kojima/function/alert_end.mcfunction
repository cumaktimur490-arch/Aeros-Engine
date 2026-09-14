# Обратная анимация "!": схлопывание перед удалением.
execute as @e[type=minecraft:text_display,tag=kj_mark] run data modify entity @s transformation.scale set value [0.05f,0.05f,0.05f]
schedule function kojima:alert_kill 6t
