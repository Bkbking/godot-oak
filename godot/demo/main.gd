extends Control

@onready var oak: Node = $OakDevice
@onready var rgb_preview: TextureRect = $Margin/VBox/Streams/RGB/Preview
@onready var ir_preview: TextureRect = $Margin/VBox/Streams/LeftIR/Preview
@onready var status: Label = $Margin/VBox/Status


func _process(_delta: float) -> void:
	var rgb_texture: Texture2D = oak.get_rgb_texture()
	if rgb_texture != null:
		rgb_preview.texture = rgb_texture

	var ir_texture: Texture2D = oak.get_left_ir_texture()
	if ir_texture != null:
		ir_preview.texture = ir_texture

	var error: String = oak.get_last_error()
	if not error.is_empty():
		status.text = "Error: " + error
		return

	status.text = "RGB frames: %d | IR izquierdo frames: %d" % [
		oak.get_rgb_frame_count(),
		oak.get_left_ir_frame_count()
	]
