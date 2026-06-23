components {
  id: "test"
  component: "/example/tests/lua_api_test.script"
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
