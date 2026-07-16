extends Control

@onready var preview: TextureRect = $Margin/VBox/Preview
@onready var status: Label = $Margin/VBox/Status

var oak: RefCounted

func _ready() -> void:
	if not ClassDB.class_exists("OakDevice") or not ClassDB.class_exists("OakStreamConfig"):
		status.text = "La GDExtension no está cargada. Compila el proyecto."
		return

	var config = ClassDB.instantiate("OakStreamConfig")
	config.width = 640
	config.height = 360
	config.fps = 60

	oak = ClassDB.instantiate("OakDevice")
	if not oak.open():
		status.text = "No se pudo abrir la OAK: " + oak.get_last_error()
		return

	if not oak.start_rgb(config):
		status.text = "No se pudo iniciar RGB: " + oak.get_last_error()
		return

	status.text = "Esperando RGB %dx%d @ %d FPS…" % [config.width, config.height, config.fps]

func _process(_delta: float) -> void:
	if oak == null:
		return

	var texture: Texture2D = oak.get_texture()
	if texture != null:
		preview.texture = texture
		status.text = "RGB %dx%d @ %d FPS — frames: %d" % [
			oak.get_active_width(),
			oak.get_active_height(),
			oak.get_active_fps(),
			oak.get_frame_count()
		]

	var error: String = oak.get_last_error()
	if not error.is_empty():
		status.text = "Error: " + error

func _exit_tree() -> void:
	if oak != null:
		oak.close()
