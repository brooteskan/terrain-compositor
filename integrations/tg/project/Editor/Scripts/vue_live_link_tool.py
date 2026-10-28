"""Small dockable O3DE tool for the Vue scene link."""

import json
import importlib
import os
import sys
import time
import uuid
from pathlib import Path

import azlmbr.legacy.general as general
import azlmbr.bus as bus
import azlmbr.editor as editor
import azlmbr.paths as paths
from PySide6 import QtCore, QtGui, QtWidgets

PROJECT = Path(paths.projectroot)
LINK_SCRIPTS = PROJECT / "Gem/Source/Editor/VueLiveLink/VueScripts"
REQUEST = PROJECT / "user/VueLiveLink/request.json"
VUE_STATUS = PROJECT / "user/VueLiveLink/vue_agent_status.json"
sys.path.insert(0, str(PROJECT / "Gem/Source/Editor/VueLiveLink"))
import link_requests


class _LazyEditorLink:
    def __getattr__(self, name):
        return getattr(importlib.import_module("editor_link"), name)


editor_link = _LazyEditorLink()

PANE_NAME = "Vue Live Link"
_registered = False
_view_handler = None
_registration_handler = None
_pane = None


class VueLiveLinkPane(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("TGVueLiveLinkPane")
        self.setMinimumWidth(320)
        layout = QtWidgets.QVBoxLayout(self)

        heading = QtWidgets.QLabel("Vue Live Link")
        font = heading.font()
        font.setPointSize(font.pointSize() + 2)
        font.setBold(True)
        heading.setFont(font)
        layout.addWidget(heading)

        self.level = QtWidgets.QLabel()
        layout.addWidget(self.level)
        level_buttons = QtWidgets.QHBoxLayout()
        initialize = QtWidgets.QPushButton("Open / initialize level")
        initialize.setToolTip("Open or create VueLiveLink, then create or repair the vue environment, sun light, and Sky Atmosphere entities")
        initialize.clicked.connect(self._initialize)
        level_buttons.addWidget(initialize)
        validate = QtWidgets.QPushButton("Validate current")
        validate.clicked.connect(self._validate)
        level_buttons.addWidget(validate)
        layout.addLayout(level_buttons)

        connection = QtWidgets.QHBoxLayout()
        self.connect = QtWidgets.QPushButton("Connect")
        self.connect.clicked.connect(self._connect)
        connection.addWidget(self.connect)
        self.disconnect = QtWidgets.QPushButton("Disconnect")
        self.disconnect.clicked.connect(self._disconnect)
        connection.addWidget(self.disconnect)
        layout.addLayout(connection)

        self.state = QtWidgets.QLabel()
        self.state.setWordWrap(True)
        self.state.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
        layout.addWidget(self.state)
        self.atmosphere = QtWidgets.QLabel()
        self.atmosphere.setWordWrap(True)
        self.atmosphere.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
        layout.addWidget(self.atmosphere)
        self.vue_state = QtWidgets.QLabel()
        self.vue_state.setWordWrap(True)
        layout.addWidget(self.vue_state)
        self.feedback = QtWidgets.QLabel()
        self.feedback.setWordWrap(True)
        self.feedback.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
        layout.addWidget(self.feedback)

        layout.addSpacing(12)
        self.sync = QtWidgets.QPushButton("Sync Vue now")
        self.sync.setToolTip("Sync Vue settings and placement; import geometry and materials for all included rocks")
        self.sync.clicked.connect(lambda: self._request_sync())
        layout.addWidget(self.sync)
        cloud_export = QtWidgets.QPushButton("Export volumetric clouds…")
        cloud_export.clicked.connect(lambda: self._run(lambda: importlib.import_module("cloud_export_dialog").show()))
        layout.addWidget(cloud_export)
        self.camera_sync = QtWidgets.QPushButton("Sync Vue camera")
        self.camera_sync.setToolTip("Copy the declared Vue camera pose and perspective projection to TG. Ordinary Sync and sky bakes never move the camera. SelectCamera.py declares the source camera.")
        self.camera_sync.clicked.connect(lambda: self._request_sync(sync_camera=True))
        layout.addWidget(self.camera_sync)
        bake_note = QtWidgets.QLabel("Bake Vue environment updates the skybox and skylight. Update Vue density on a finite cloud exports, prepares and binds its density asset. Unbounded atmospheric clouds remain in the sky dome.")
        bake_note.setWordWrap(True)
        layout.addWidget(bake_note)
        aim = QtWidgets.QPushButton("Aim viewport at Vue sun")
        aim.setToolTip("Point the Editor camera at the latest synced Vue sun direction")
        aim.clicked.connect(lambda: self._run(editor_link.aim_view_at_vue_sun))
        layout.addWidget(aim)
        sunlight = QtWidgets.QPushButton("Sunlight settings…")
        sunlight.setToolTip("Derive incident sunlight, control shadow approximation, and separate the solar disc from IBL")
        sunlight.clicked.connect(lambda: self._run(lambda: importlib.import_module("sunlight_editor").show()))
        layout.addWidget(sunlight)
        environment = QtWidgets.QPushButton("Environment and cloud lighting…")
        environment.clicked.connect(lambda: self._run(lambda: importlib.import_module("environment_editor").show()))
        layout.addWidget(environment)
        aerial = QtWidgets.QPushButton("Aerial perspective settings…")
        aerial.setToolTip("Depth-dependent haze, fog and directional scattering on scene geometry")
        aerial.clicked.connect(lambda: self._run(lambda: importlib.import_module("aerial_editor").show()))
        layout.addWidget(aerial)
        ambient = QtWidgets.QPushButton("Ambient lighting and GI settings…")
        ambient.setToolTip("Independent diffuse sky, uniform fill, AO and bounded indirect lighting")
        ambient.clicked.connect(lambda: self._run(lambda: importlib.import_module("ambient_editor").show()))
        layout.addWidget(ambient)
        clouds = QtWidgets.QPushButton("Cloud materials…")
        clouds.setToolTip("Inspect Vue cloud materials and refresh their baked sky and skylight")
        clouds.clicked.connect(lambda: self._run(lambda: importlib.import_module("cloud_material_editor").show()))
        layout.addWidget(clouds)
        materials = QtWidgets.QPushButton("Material lighting response…")
        materials.setToolTip("Per-terrain diffuse, specular and normal response with reversible material assignments")
        materials.clicked.connect(lambda: self._run(lambda: importlib.import_module("material_response_editor").show()))
        layout.addWidget(materials)
        camera_response = QtWidgets.QPushButton("Camera response…")
        camera_response.setToolTip("Scene-wide highlight compression, midtones, shadow lift and fixed exposure")
        camera_response.clicked.connect(lambda: self._run(lambda: importlib.import_module("camera_response_editor").show()))
        layout.addWidget(camera_response)
        calibration = QtWidgets.QPushButton("Lighting calibration…")
        calibration.setToolTip("Compare matching gray cards in Vue and O3DE and apply measured sky/sun corrections")
        calibration.clicked.connect(lambda: self._run(
            lambda: importlib.import_module("lighting_calibration_editor").show()))
        layout.addWidget(calibration)
        note = QtWidgets.QLabel("One-time Vue setup: Automation → Run Python Script… → StartLiveLink.py. Save the Vue scene first.")
        note.setWordWrap(True)
        layout.addWidget(note)
        for name, description in (("StartLiveLink.py", "Copy Vue setup script path"),
                                  ("SelectSun.py", "Select the source sun in Vue"),
                                  ("SelectCamera.py", "Declare the render camera in Vue"),
                                  ("IncludeSelected.py", "Copy object-marking script path")):
            row = QtWidgets.QHBoxLayout()
            label = QtWidgets.QLabel(description)
            row.addWidget(label)
            button = QtWidgets.QPushButton("Copy " + name)
            button.setToolTip(str(LINK_SCRIPTS / name))
            button.clicked.connect(lambda checked=False, script=name: self._copy_script(script))
            row.addWidget(button)
            layout.addLayout(row)
        note = QtWidgets.QLabel("Sync maintains sources under vue/terrains, vue/clouds and vue/objects, and imports geometry and materials for every included rock. Update object refreshes one source. ExcludeSelected.py removes a source after a complete sync; IncludeSelected.py includes it again.")
        note.setWordWrap(True)
        layout.addWidget(note)
        folder = QtWidgets.QPushButton("Open Vue scripts folder")
        folder.clicked.connect(self._open_scripts)
        layout.addWidget(folder)
        layout.addStretch()

        self.refresh_timer = QtCore.QTimer(self)
        self.refresh_timer.setInterval(500)
        self.refresh_timer.timeout.connect(self._refresh)
        self.refresh_timer.start()
        self._refresh()

    def _run(self, operation):
        try:
            value = operation()
            self.feedback.setStyleSheet("")
            self.feedback.setText("Ready: " + str(value))
        except Exception as error:
            self.feedback.setStyleSheet("color: #e28b8b;")
            self.feedback.setText(str(error))
            print("Vue Live Link action did not start: " + str(error))
        self._refresh()

    def _initialize(self):
        self._run(lambda: editor_link.initialize(prompt=True))

    def _validate(self):
        self._run(editor_link.validate_current)

    def _connect(self):
        self._run(lambda: editor_link.start().level)

    def _disconnect(self):
        self._run(lambda: editor_link.stop() or "Disconnected")

    def _request_sync(self, bake_sky=False, terrain_uid=None, sync_camera=False, object_uid=None, object_link_id=None, material_policy=None):
        def request():
            if sync_camera and (bake_sky or terrain_uid):
                raise RuntimeError("Camera sync is a separate action")
            if object_uid is not None and (sync_camera or bake_sky or terrain_uid):
                raise RuntimeError("Update object is a separate action")
            if not editor_link._SESSION or not editor_link._SESSION.timer.isActive():
                raise RuntimeError("Click Connect in TG first")
            if (str(general.get_current_level_name()) != editor_link._SESSION.level or
                    general.is_in_game_mode()):
                raise RuntimeError("Open the connected level in Editor before sending a Vue request")
            status = self._vue_agent_status()
            if not status:
                raise RuntimeError("Vue agent is offline. In Vue, run StartLiveLink.py from Automation → Run Python Script…")
            if status.get("phase") == "startup failed":
                raise RuntimeError("Vue agent could not start: " + str(status.get("error", "Unknown startup error")))
            link = json.loads((PROJECT / "user/VueLiveLink/link.json").read_text(encoding="utf-8"))
            if status.get("scene") != link.get("scene"):
                raise RuntimeError("Vue agent scene differs from the scene linked to TG")
            if terrain_uid is not None:
                editor_link.terrain_entity_by_uid(terrain_uid, link["linkId"])
                return editor_link._SESSION.queue_terrain_bake(terrain_uid, link["linkId"])
            if getattr(editor_link._SESSION, "cloud_job", None) and not editor_link._SESSION.cloud_job.done:
                raise RuntimeError("Finish or cancel the cloud export before starting another request")
            if getattr(editor_link._SESSION, "object_job", None) and not editor_link._SESSION.object_job.done:
                raise RuntimeError("Finish the object import before starting another request")
            if editor_link._SESSION.phase in ("terrain processing", "converting", "processing", "loading"):
                raise RuntimeError("Finish the current Vue bake before starting another request")
            if (link_requests.owns(REQUEST, status.get("requestId")) and
                    status.get("phase", "").startswith(("baking ", "exporting"))):
                raise RuntimeError("Vue is still handling the previous bake")
            if object_uid is not None:
                if object_link_id != link["linkId"]:
                    raise RuntimeError("This object belongs to another Vue link")
                import object_source_editor
                object_source_editor.find(object_uid, link["linkId"])
                selected_policy = material_policy or "vuepbr"
                if selected_policy not in ("default", "vuepbr"):
                    raise RuntimeError("Unknown Vue object material policy")
            if REQUEST.is_file():
                pending = json.loads(REQUEST.read_text(encoding="utf-8"))
                if pending.get("requestId") != status.get("requestId"):
                    raise RuntimeError("Vue has not acknowledged the previous request yet")
            request_id = uuid.uuid4().hex
            REQUEST.parent.mkdir(parents=True, exist_ok=True)
            temporary = REQUEST.with_suffix(".json.writing")
            request_data = {"requestId": request_id, "time": time.time(),
                                             "bakeSky": bool(bake_sky),
                                             "syncCamera": bool(sync_camera),
                                             "bakeTerrainUid": terrain_uid}
            if object_uid is not None:
                request_data.update(operation="updateObject", objectUid=object_uid,
                                    linkId=link["linkId"], materialPolicy=selected_policy)
            temporary.write_text(json.dumps(request_data) + "\n", encoding="utf-8")
            os.replace(temporary, REQUEST)
            if object_uid is not None:
                return "Requested Vue object update " + request_id[:8]
            return ("Requested Vue camera sync " if sync_camera else
                    "Requested terrain bake " if terrain_uid else
                    "Requested Vue environment bake " if bake_sky else
                    "Requested Vue sync ") + request_id[:8]
        def request_with_feedback():
            try:
                return request()
            except Exception as error:
                if terrain_uid is not None:
                    try:
                        link_id = json.loads((PROJECT / "user/VueLiveLink/link.json").read_text(encoding="utf-8"))["linkId"]
                        owner, component, data = editor_link.terrain_entity_by_uid(terrain_uid, link_id)
                        updated = dict(data, bakeStatus="Request blocked", bakeDetail=str(error))
                        editor_link._set_vue_snapshot(owner, component, updated,
                            editor.VueTerrainSourceRequestBus, "Vue terrain data")
                    except Exception as status_error:
                        print("Vue terrain bake request status could not be recorded: " + str(status_error))
                raise
        self._run(request_with_feedback)

    def _copy_script(self, name):
        QtWidgets.QApplication.clipboard().setText(str(LINK_SCRIPTS / name))
        self.feedback.setStyleSheet("")
        self.feedback.setText("Copied path for " + name)

    def _open_scripts(self):
        QtGui.QDesktopServices.openUrl(QtCore.QUrl.fromLocalFile(str(LINK_SCRIPTS)))

    def _vue_agent_status(self):
        try:
            status = json.loads(VUE_STATUS.read_text(encoding="utf-8"))
            status = link_requests.agent_status(REQUEST, status)
            active_bake = (link_requests.owns(REQUEST, status.get("requestId")) and
                           status.get("phase", "").startswith(("exporting", "baking ")))
            window = 3600 if active_bake else 60
            return status if time.time() - float(status["time"]) < window else None
        except (OSError, ValueError, KeyError, TypeError):
            return None

    def _refresh(self):
        current = str(general.get_current_level_name()) or "No level open"
        self.level.setText("Current level: " + current)
        try:
            session = editor_link._SESSION
        except Exception as error:
            self.refresh_timer.stop()
            self.state.setText("Vue Live Link could not load: " + str(error))
            return
        connected = bool(session and session.timer.isActive())
        self.connect.setEnabled(not connected)
        self.disconnect.setEnabled(connected)
        agent = self._vue_agent_status()
        self.vue_state.setText("Vue agent: " + (agent.get("phase", "ready") if agent else "offline"))
        if not connected:
            self.state.setText("Disconnected")
            self.atmosphere.setText("")
            return
        status = editor_link.STATUS
        try:
            value = json.loads(status.read_text(encoding="utf-8"))
            phase = value.get("phase", "listening")
            detail = value.get("detail", "")
            suffix = "\n" + detail if detail else ""
            if current != session.level:
                suffix += "\nPaused while another level is open"
            if value.get("iblStale"):
                suffix += "\nSky lighting is stale; bake the environment for the current atmosphere"
            if value.get("cloudMaterials", {}).get("needsBake"):
                suffix += "\nCloud materials changed; bake the environment"
            cloud_sources = value.get("cloudSources")
            object_sources = value.get("objectSources")
            if object_sources is not None:
                suffix += ("\n%d object sources under vue/objects" % object_sources.get("count", 0)
                           if object_sources.get("complete") else "\nObject discovery incomplete; existing sources retained")
                for error in object_sources.get("acquisition", {}).get("errors", [])[:2]:
                    suffix += "\n" + error.get("error", "Unknown object acquisition error")
            if cloud_sources is not None:
                if cloud_sources.get("complete"):
                    suffix += "\n%d cloud sources under vue/clouds" % cloud_sources.get("count", 0)
                else:
                    suffix += "\nCloud discovery incomplete; existing cloud entities retained"
            self.state.setText("Connection: " + phase + suffix)
            atmosphere = value.get("appliedAtmosphere") or {}
            if atmosphere:
                parts = ["Vue sun settings: size %.2f, visibility %.2f" %
                         (atmosphere["sunSize"], atmosphere["sunVisibility"])]
                if "sunAzimuth" in atmosphere and "sunPitch" in atmosphere:
                    parts.append("azimuth %.1f°, pitch %.1f°" %
                                 (atmosphere["sunAzimuth"], atmosphere["sunPitch"]))
                if "lightColor" in atmosphere:
                    parts.append("light RGB " + ", ".join("%.2f" % value for value in atmosphere["lightColor"]))
                if "cloudLayerCount" in atmosphere:
                    parts.append("%d cloud layers" % atmosphere["cloudLayerCount"])
                self.atmosphere.setText("; ".join(parts))
            else:
                self.atmosphere.setText("No Vue atmosphere applied yet")
        except (OSError, ValueError, KeyError, TypeError):
            self.state.setText("Connection: listening")
            self.atmosphere.setText("")


def open_tool():
    if not _registered:
        raise RuntimeError("Vue Live Link view pane is not registered")
    general.open_pane(PANE_NAME)


def request_environment_bake():
    """Called by Vue Source on the combined environment entity."""
    if _pane is None:
        open_tool()
    if _pane is None:
        raise RuntimeError("Open the Vue Live Link tool and click Connect first")
    _pane._request_sync(True)


def request_terrain_bake(uid):
    """Called by the Bake terrain button on an identified terrain entity."""
    if _pane is None:
        open_tool()
    if _pane is None:
        raise RuntimeError("Open the Vue Live Link tool and click Connect first")
    _pane._request_sync(terrain_uid=uid)


def request_object_update(uid, link_id, material_policy=None):
    """Called by the Update object button on a specific persistent source."""
    if _pane is None:
        open_tool()
    if _pane is None:
        raise RuntimeError("Open the Vue Live Link tool and click Connect first")
    _pane._request_sync(object_uid=uid, object_link_id=link_id, material_policy=material_policy)


def request_density_update(entity_id):
    """Called by Vue Source on the specific finite cloud entity."""
    if _pane is None:
        open_tool()
    if _pane is None:
        raise RuntimeError("Open the Vue Live Link tool and click Connect first")
    _pane._run(lambda: importlib.import_module("cloud_density_dialog").show(entity_id))


def request_aerial_settings(entity_id):
    """Open the clicked atmosphere's controls without requiring Connect."""
    try:
        return importlib.import_module("aerial_editor").show(entity_id)
    except Exception as error:
        QtWidgets.QMessageBox.warning(QtWidgets.QApplication.activeWindow(), "Aerial effects", str(error))


def register():
    global _registered, _view_handler, _registration_handler, _pane
    if _registered:
        return

    def create_view_pane_widget(_parameters):
        global _pane
        _pane = VueLiveLinkPane()
        return _pane.winId()

    def register_view(_parameters):
        editor.EditorRequestBus(bus.Broadcast, "RegisterCustomViewPane",
                                PANE_NAME, "Tools", editor.ViewPaneOptions())

    _registration_handler = bus.NotificationHandler("EditorEventBus")
    _registration_handler.connect()
    _registration_handler.add_callback("NotifyRegisterViews", register_view)
    _view_handler = bus.NotificationHandler("ViewPaneCallbackBus")
    _view_handler.connect(PANE_NAME)
    _view_handler.add_callback("CreateViewPaneWidget", create_view_pane_widget)
    register_view(None)
    _registered = True
    import terrain_overlap_editor
    terrain_overlap_editor.start()
    import collision_surface_editor
    collision_surface_editor.start()
    print("Vue Live Link registered as an Editor Tools view pane")
