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
