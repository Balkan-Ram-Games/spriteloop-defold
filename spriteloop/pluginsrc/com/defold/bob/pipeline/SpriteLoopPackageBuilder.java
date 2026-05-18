package com.dynamo.bob.pipeline;

import com.dynamo.bob.BuilderParams;
import com.dynamo.bob.CopyBuilder;

// Copies .spla packages into the build output when referenced by .spriteloop components.
//
// Without this builder, SpriteLoop packages only work when the app manually lists their folder
// in game.project custom_resources. Component package references should behave like real
// resource dependencies, so Bob needs a builder for the raw .spla extension.
@BuilderParams(name = "SpriteLoopPackage", inExts = ".spla", outExt = ".spla")
public class SpriteLoopPackageBuilder extends CopyBuilder {
}
