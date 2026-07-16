extends Control

@onready var preview: TextureRect = $Margin/VBox/Preview
@onready var status: Label = $Margin/VBox/Status

var oak: RefCounted

func _ready() -> void:
    if not ClassDB.class_exists("OakDevice"):
        status.text = "OakDevice no está registrado. Compila la GDExtension."
        return

    oak = ClassDB.instantiate("OakDevice")
    if not oak.open():
        status.text = "No se pudo abrir la OAK: " + oak.get_last_error()
        return

    if not oak.start_rgb(640, 360, 30):
        status.text = "No se pudo iniciar RGB: " + oak.get_last_error()
        return

    status.text = "Esperando frames RGB…"

func _process(_delta: float) -> void:
    if oak == null:
        return
    var texture: Texture2D = oak.get_texture()
    if texture != null:
        preview.texture = texture
        status.text = "RGB activo — frames: %d" % oak.get_frame_count()
    var error: String = oak.get_last_error()
    if not error.is_empty():
        status.text = "Error: " + error

func _exit_tree() -> void:
    if oak != null:
        oak.close()
