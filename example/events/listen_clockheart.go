components {
  id: "controller"
  component: "/example/events/listen_events.script"
}
embedded_components {
  id: "clockheart"
  type: "spriteloop"
  data: "package: \"/example/events/clockheart.spla\"\n"
  "default_animation: \"run_copy\"\n"
  "material: \"/spriteloop/spriteloop/materials/spriteloop.material\"\n"
  "default_skin: \"default\"\n"
  ""
  scale {
    x: 0.35
    y: 0.35
  }
}
