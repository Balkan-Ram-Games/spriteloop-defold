package com.dynamo.bob.pipeline;

import java.io.IOException;

import com.dynamo.bob.BuilderParams;
import com.dynamo.bob.CompileExceptionError;
import com.dynamo.bob.ProtoBuilder;
import com.dynamo.bob.ProtoParams;
import com.dynamo.bob.Task;
import com.dynamo.bob.fs.IResource;
import com.dynamo.bob.pipeline.BuilderUtil;
import com.dynamo.spriteloop.proto.SpriteLoop.SpriteLoopDesc;

// Bob builder for editor-authored .spriteloop component resources.
//
// The editor saves a SpriteLoopDesc message in a .spriteloop file. Bob invokes this builder to
// declare dependencies, validate the referenced material, and emit a compiled .spriteloopc DDF
// resource consumed by the native Defold component type.
@ProtoParams(srcClass = SpriteLoopDesc.class, messageClass = SpriteLoopDesc.class)
@BuilderParams(name = "SpriteLoop", inExts = ".spriteloop", outExt = ".spriteloopc")
public class SpriteLoopBuilder extends ProtoBuilder<SpriteLoopDesc.Builder> {
    private static final String DEFAULT_MATERIAL = "/spriteloop/spriteloop/materials/spriteloop.material";

    // Returns the source .material path from the editor data, falling back to the adapter default.
    // builder is the parsed SpriteLoopDesc from the .spriteloop resource.
    private String getMaterialSourcePath(SpriteLoopDesc.Builder builder) {
        String material = builder.getMaterial();
        return material.equals("") ? DEFAULT_MATERIAL : material;
    }

    // Creates the Bob task for one .spriteloop input.
    // resource is the source asset; the task output is the matching .spriteloopc resource and the
    // configured material is added as a dependency so Bob rebuilds when it changes.
    @Override
    public Task create(IResource resource) throws IOException, CompileExceptionError {
        Task.TaskBuilder taskBuilder = Task.newBuilder(this)
                .setName(this.params.name())
                .addInput(resource)
                .addOutput(resource.changeExt(this.params.outExt()));

        SpriteLoopDesc.Builder builder = getSrcBuilder(resource);
        createSubTasks(builder, taskBuilder);
        if (!builder.getPackage().equals("")) {
            createSubTask(builder.getPackage(), "package", taskBuilder);
        }
        createSubTask(getMaterialSourcePath(builder), "material", taskBuilder);
        return taskBuilder.build();
    }

    // Validates and rewrites resource paths before serialization.
    // task is the current Bob task, resource is the source .spriteloop file, and builder is the
    // mutable DDF message that will be emitted as .spriteloopc.
    @Override
    protected SpriteLoopDesc.Builder transform(Task task, IResource resource, SpriteLoopDesc.Builder builder)
            throws CompileExceptionError {
        String material = getMaterialSourcePath(builder);
        String packagePath = builder.getPackage();
        if (!packagePath.equals("")) {
            BuilderUtil.checkResource(this.project, resource, "package", packagePath);
        }
        BuilderUtil.checkResource(this.project, resource, "material", material);
        builder.setMaterial(BuilderUtil.replaceExt(material, ".material", ".materialc"));
        return builder;
    }
}
