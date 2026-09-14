# KOJIMA PACK — инициализация. Запускается автоматически при /reload (см. load.json).
scoreboard objectives add kj_dmg minecraft.custom:minecraft.damage_taken "§cПолученный урон"
scoreboard objectives add kj_cd dummy "§7Кулдаун алерта"
scoreboard objectives add kj_go trigger "§6Трюки Кодзимы"
tellraw @a [{"text":"[KOJIMA] ","color":"gold"},{"text":"datapack загружен. ","color":"gray"},{"text":"/trigger kj_go set 1","color":"yellow"},{"text":" = Фултон, ","color":"gray"},{"text":"set 2","color":"yellow"},{"text":" = алерт, ","color":"gray"},{"text":"set 3","color":"yellow"},{"text":" = чистка","color":"gray"}]
