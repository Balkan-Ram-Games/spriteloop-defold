components {
  id: "collisionobject"
  component: "/example/main/player.collisionobject"
}
components {
  id: "controller"
  component: "/example/main/player.script"
}
components {
  id: "destroy_robot_debug"
  component: "/example/main/destroy_robot_debug.script"
}
embedded_components {
  id: "robot"
  type: "spriteloop"
  data: "package: \"/example/assets/robot_idle.spla\"\n"
  "material: \"/spriteloop/spriteloop/materials/spriteloop.material\"\n"
  "autoplay: false\n"
  ""
  position {
    x: -192.0
    y: -117.0
  }
  rotation {
    z: -0.14655253
    w: 0.9892029
  }
  scale {
    x: 0.4
    y: 0.4
  }
}
embedded_components {
  id: "robot1"
  type: "spriteloop"
  data: "package: \"/example/assets/robot_idle.spla\"\n"
  "material: \"/spriteloop/spriteloop/materials/spriteloop.material\"\n"
  ""
  scale {
    x: 0.4
    y: 0.4
  }
}
embedded_components {
  id: "robot2"
  type: "spriteloop"
  data: "package: \"/example/assets/robot_idle.spla\"\n"
  "material: \"/spriteloop/spriteloop/materials/spriteloop.material\"\n"
  ""
  position {
    x: 105.0
    y: 170.0
  }
  rotation {
    z: 0.97325546
    w: 0.22972551
  }
  scale {
    x: 0.4
    y: 0.4
  }
}
embedded_components {
  id: "robot3"
  type: "spriteloop"
  data: "package: \"/example/assets/robot.spla\"\n"
  "default_animation: \"test\"\n"
  "material: \"/spriteloop/spriteloop/materials/spriteloop.material\"\n"
  ""
  position {
    x: 257.0
    y: 306.0
  }
  rotation {
    z: 0.18496804
    w: 0.9827445
  }
  scale {
    x: 0.4
    y: 0.4
  }
}
embedded_components {
  id: "robot4"
  type: "spriteloop"
  data: "package: \"/example/assets/invalid.spla\"\n"
  "material: \"/spriteloop/spriteloop/materials/spriteloop.material\"\n"
  ""
  position {
    x: 501.0
    y: 297.0
  }
  rotation {
    z: -0.14715712
    w: 0.98911315
  }
  scale {
    x: 0.4
    y: 0.4
  }
}
