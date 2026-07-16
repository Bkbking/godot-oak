extends Control

@onready var oak: Node = $OakDevice
@onready var rgb_preview: TextureRect = $Margin/VBox/Streams/RGB/Preview
@onready var left_ir_preview: TextureRect = $Margin/VBox/Streams/LeftIR/Preview
@onready var right_ir_preview: TextureRect = $Margin/VBox/Streams/RightIR/Preview
@onready var status: Label = $Margin/VBox/Status

var _metrics_timer := 0.0


func _process(delta: float) -> void:
	var rgb_texture: Texture2D = oak.get_rgb_texture()
	if rgb_texture != null:
		rgb_preview.texture = rgb_texture

	oak.update_stereo_ir_textures()

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

	var metrics := _build_metrics_line()
	status.text = metrics

	_metrics_timer += delta
	if _metrics_timer >= 1.0:
		_metrics_timer = 0.0
		print(metrics)


func _build_metrics_line() -> String:
	return (
        "RGB r/p/d %d/%d/%d cap-host=%.2fms host-tex=%.2fms upload=%.2fms | "
		+ "STEREO pairs/mismatch=%d/%d skew=%.3fms | "
		+ "IR-L r/p/d %d/%d/%d cap-host=%.2fms host-tex=%.2fms upload-pair=%.2fms | "
		+ "IR-R r/p/d %d/%d/%d cap-host=%.2fms host-tex=%.2fms upload-pair=%.2fms"
	) % [
		oak.get_rgb_frame_count(),
		oak.get_rgb_presented_count(),
		oak.get_rgb_dropped_count(),
		oak.get_rgb_capture_to_host_ms(),
		oak.get_rgb_host_latency_ms(),
		oak.get_rgb_texture_update_ms(),

		oak.get_stereo_pair_count(),
		oak.get_stereo_mismatch_count(),
		oak.get_stereo_timestamp_skew_ms(),

		oak.get_left_ir_frame_count(),
		oak.get_left_ir_presented_count(),
		oak.get_left_ir_dropped_count(),
		oak.get_left_ir_capture_to_host_ms(),
		oak.get_left_ir_host_latency_ms(),
		oak.get_left_ir_texture_update_ms(),

		oak.get_right_ir_frame_count(),
		oak.get_right_ir_presented_count(),
		oak.get_right_ir_dropped_count(),
		oak.get_right_ir_capture_to_host_ms(),
		oak.get_right_ir_host_latency_ms(),
		oak.get_right_ir_texture_update_ms()
	]
