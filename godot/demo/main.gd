extends Control

@onready var oak: Node = $OakDevice
@onready var rgb_preview: TextureRect = $Margin/VBox/Streams/RGB/Preview
@onready var left_ir_preview: TextureRect = $Margin/VBox/Streams/LeftIR/Preview
@onready var right_ir_preview: TextureRect = $Margin/VBox/Streams/RightIR/Preview
@onready var status: Label = $Margin/VBox/Status


func _process(_delta: float) -> void:
	var rgb_texture: Texture2D = oak.get_rgb_texture()
	if rgb_texture != null:
		rgb_preview.texture = rgb_texture

	var left_texture: Texture2D = oak.get_left_ir_texture()
	if left_texture != null:
		left_ir_preview.texture = left_texture

	var right_texture: Texture2D = oak.get_right_ir_texture()
	if right_texture != null:
		right_ir_preview.texture = right_texture

	var error: String = oak.get_last_error()
	if not error.is_empty():
		status.text = "Error: " + error
		return

	status.text = "RGB: %d | IR izq.: %d | IR der.: %d frames" % [
		oak.get_rgb_frame_count(),
		oak.get_left_ir_frame_count(),
		oak.get_right_ir_frame_count()
	]
