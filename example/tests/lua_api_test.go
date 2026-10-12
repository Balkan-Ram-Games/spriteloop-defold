components {
  id: "test"
  component: "/example/tests/lua_api_test.script"
}
components {
  id: "results"
  component: "/example/tests/lua_api_test.gui"
}
embedded_components {
  id: "ranger_fixture"
  type: "spriteloop"
  data: "package: \"/example/assets/ranger_idle.spla\"\nvisible: false\nautoplay: false\n"
}
embedded_components {
  id: "robot"
  type: "spriteloop"
  data: "package: \"/example/assets/robot_idle_skins.spla\"\n"
  "material: \"/spriteloop/spriteloop/materials/spriteloop.material\"\n"
  ""
  scale {
    x: 0.18
    y: 0.18
  }
}
embedded_components {
  id: "event_robot"
  type: "spriteloop"
  data: "package: \"/example/tests/assets/clockheart.spla\"\n"
  "material: \"/spriteloop/spriteloop/materials/spriteloop.material\"\n"
  "default_animation: \"run\"\n"
  "loop: true\n"
  "visible: false\n"
  "autoplay: false\n"
  ""
}
