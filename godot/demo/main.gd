extends Control

@onready var oak: Node = $OakDevice
@onready var preview: TextureRect = $Margin/VBox/Preview
@onready var status: Label = $Margin/VBox/Status


func _process(_delta: float) -> void:
	if oak == null:
		return

	var texture: Texture2D = oak.get_texture()
	if texture != null:
		preview.texture = texture

	var error: String = oak.get_last_error()
	if not error.is_empty():
		status.text = "Error: " + error
		return

	if oak.is_streaming():
		status.text = "RGB %dx%d @ %d FPS — frames: %d" % [
			oak.get_active_width(),
			oak.get_active_height(),
			oak.get_active_fps(),
			oak.get_frame_count()
		]
	else:
		status.text = "Esperando a la cámara OAK…"
