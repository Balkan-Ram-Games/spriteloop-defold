components {
  id: "controller"
  component: "/example/bench/bench_controller.script"
}
embedded_components {
  id: "robot_factory"
  type: "factory"
  data: "prototype: \"/example/bench/bench_robot.go\"\n"
  ""
}
embedded_components {
  id: "robot_50_factory"
  type: "factory"
  data: "prototype: \"/example/bench/bench_robot_50.go\"\n"
  ""
}
embedded_components {
  id: "robot_27_factory"
  type: "factory"
  data: "prototype: \"/example/bench/bench_robot_27.go\"\n"
  ""
}
embedded_components {
  id: "sprite_tilesource_small_factory"
  type: "factory"
  data: "prototype: \"/example/bench/sprite/robot_sprite_tilesource_small.go\"\n"
  ""
}
embedded_components {
  id: "sprite_tilesource_full_factory"
  type: "factory"
  data: "prototype: \"/example/bench/sprite/robot_sprite_tilesource_full.go\"\n"
  ""
}
embedded_components {
  id: "sprite_atlas_factory"
  type: "factory"
  data: "prototype: \"/example/bench/sprite/robot_sprite_atlas.go\"\n"
  ""
}
embedded_components {
  id: "sprite_atlas_trim_factory"
  type: "factory"
  data: "prototype: \"/example/bench/sprite/robot_sprite_atlas_trim.go\"\n"
  ""
}
