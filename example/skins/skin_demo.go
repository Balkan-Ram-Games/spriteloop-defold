components {
  id: "controller"
  component: "/example/skins/skin_demo.script"
}
embedded_components {
  id: "robot_factory"
  type: "factory"
  data: "prototype: \"/example/skins/skin_robot.go\"\n"
  ""
}
embedded_components {
  id: "robot"
  type: "spriteloop"
  data: "package: \"/example/assets/robot_idle_z.spla\"\n"
  "material: \"/spriteloop/spriteloop/materials/spriteloop.material\"\n"
  "default_skin: \"blue_robot\"\n"
  ""
  scale {
    x: 0.38
    y: 0.38
  }
}
