;; Defold editor plugin for the SpriteLoop component resource.
;;
;; The editor loads this namespace from the extension plugin. It registers the .spriteloop
;; resource type, exposes inspector fields, contributes build targets, and draws a non-fatal
;; static scene preview by reading frame 0 from the referenced .spla package.
(ns editor.spriteloop-ext
  (:require [clojure.data.json :as json]
            [clojure.java.io :as io]
            [dynamo.graph :as g]
            [editor.build-target :as bt]
            [editor.colors :as colors]
            [editor.geom :as geom]
            [editor.gl :as gl]
            [editor.gl.pass :as pass]
            [editor.gl.shader :as shader]
            [editor.gl.texture :as texture]
            [editor.gl.vertex2 :as vtx]
            [editor.defold-project :as project]
            [editor.graph-util :as gu]
            [editor.localization :as localization]
            [editor.properties :as properties]
            [editor.protobuf :as protobuf]
            [editor.resource :as resource]
            [editor.resource-node :as resource-node]
            [editor.scene-picking :as scene-picking]
            [editor.validation :as validation]
            [editor.workspace :as workspace])
  (:import [com.jogamp.opengl GL GL2]
           [editor.gl.vertex2 VertexBuffer]
           [editor.types AABB]
           [java.awt AlphaComposite RenderingHints]
           [java.awt.geom AffineTransform]
           [java.awt.image BufferedImage]
           [java.io ByteArrayInputStream ByteArrayOutputStream]
           [java.nio ByteBuffer]
           [javax.imageio ImageIO]
           [javax.vecmath Matrix4d Point3d]))

(set! *warn-on-reflection* true)

(def ^:private spriteloop-ext "spriteloop")
(def ^:private spla-ext "spla")
(def ^:private spriteloop-template "/spriteloop/editor/resources/templates/template.spriteloop")
(def ^:private default-material "/spriteloop/spriteloop/materials/spriteloop.material")
(def ^:private spriteloop-icon "/spriteloop/editor/resources/icons/loop-arrow-32.png")
(def ^:private fallback-canvas-size [128.0 128.0])

;; Vertex layouts used by the editor scene passes.
(vtx/defvertex color-vtx
  (vec3 position)
  (vec4 color))

(vtx/defvertex position-vtx
  (vec3 position))

(vtx/defvertex preview-vtx
  (vec3 position)
  (vec2 texcoord0)
  (vec4 color))

(shader/defshader outline-vertex-shader
  (attribute vec4 position)
  (attribute vec4 color)
  (varying vec4 var_color)
  (defn void main []
    (setq gl_Position (* gl_ModelViewProjectionMatrix position))
    (setq var_color color)))

(shader/defshader outline-fragment-shader
  (varying vec4 var_color)
  (defn void main []
    (setq gl_FragColor var_color)))

(def outline-shader
  (shader/make-shader ::outline-shader outline-vertex-shader outline-fragment-shader))

(shader/defshader pick-vertex-shader
  (uniform mat4 view_proj)
  (attribute vec3 position)
  (defn void main []
    (setq gl_Position (* view_proj (vec4 position 1.0)))))

(shader/defshader pick-fragment-shader
  (uniform vec4 id)
  (defn void main []
    (setq gl_FragColor id)))

(def pick-shader
  (shader/make-shader ::pick-shader pick-vertex-shader pick-fragment-shader {"view_proj" :view-proj "id" :id}))

(shader/defshader preview-vertex-shader
  (attribute vec4 position)
  (attribute vec2 texcoord0)
  (attribute vec4 color)
  (varying vec2 var_texcoord0)
  (varying vec4 var_color)
  (defn void main []
    (setq gl_Position (* gl_ModelViewProjectionMatrix position))
    (setq var_texcoord0 texcoord0)
    (setq var_color color)))

(shader/defshader preview-fragment-shader
  (uniform sampler2D texture_sampler)
  (varying vec2 var_texcoord0)
  (varying vec4 var_color)
  (defn void main []
    (setq gl_FragColor (* (texture2D texture_sampler var_texcoord0) var_color))))

(def preview-shader
  (shader/make-shader ::preview-shader preview-vertex-shader preview-fragment-shader))

(def ^:private preview-texture-params
  {:min-filter GL/GL_LINEAR
   :mag-filter GL/GL_LINEAR
   :wrap-s GL/GL_CLAMP_TO_EDGE
   :wrap-t GL/GL_CLAMP_TO_EDGE})

(defonce ^:private preview-image-cache (atom {}))
(def ^:private preview-image-cache-limit 16)

;; Loads the generated Java DDF class created by the Bob plugin jar.
;; Returns nil instead of throwing so a missing plugin jar disables only the editor type.
(defn- load-spriteloop-desc-cls []
  (try
    (workspace/load-class! "com.dynamo.spriteloop.proto.SpriteLoop$SpriteLoopDesc")
    (catch Throwable e
      (println "SpriteLoop editor plugin disabled: generated plugin class is unavailable."
               "Build spriteloop/plugins/share/pluginSpriteLoopExt.jar before enabling the editor resource type."
               (.getMessage e))
      nil)))

(def ^:private spriteloop-desc-cls
  (delay (load-spriteloop-desc-cls)))

;; Converts a property validation function into a Defold editor fatal property error.
;; prop-kw identifies the field, validate-fn returns an error string or nil, node-id scopes
;; the error, and value is the property value being checked.
(defn- validate-property [prop-kw validate-fn node-id value]
  (validation/prop-error :fatal node-id prop-kw validate-fn value (validation/keyword->name prop-kw)))

;; Validation helper used by required string fields.
(defn- prop-empty? [value prop-name]
  (when (empty? value)
    (format "'%s' must be specified" prop-name)))

;; Validation helper for the required package path. Defold reports embedded
;; component validation against the owning .go file, so name SpriteLoop in the
;; message to make the failing component type clear.
(defn- package-empty? [value _prop-name]
  (when (nil? value)
    "SpriteLoop component 'Package' must be specified"))

;; Validates that the package field is not empty.
(defn- validate-package [node-id value]
  (or (validate-property :package package-empty? node-id value)
      (validation/prop-error :fatal node-id :package validation/prop-resource-not-exists? value "Package")))

;; Validates that the material field points to an existing Defold material resource.
(defn- validate-material [node-id value]
  (or (validation/prop-error :fatal node-id :material validation/prop-nil? value "Material")
      (validation/prop-error :fatal node-id :material validation/prop-resource-not-exists? value "Material")))

;; Produces the compiled .spriteloopc build content.
;; resource is the output build resource, dep-resources contains resolved dependency outputs,
;; and user-data carries the editor DDF map plus the labels of dependent resources to rewrite.
(defn- build-spriteloop [resource dep-resources user-data]
  (let [pb (:spriteloop-pb user-data)
        pb (reduce #(assoc %1 (first %2) (second %2))
                   pb
                   (map (fn [[label res]]
                          [label (resource/proj-path (get dep-resources res))])
                        (:dep-resources user-data)))
        content (protobuf/map->bytes @spriteloop-desc-cls pb)]
    {:resource resource
     :content content}))

;; Copies a referenced .spla package into the editor build output.
;; The Bob command-line builder has a Java CopyBuilder for .spla files, but the editor build graph
;; is produced here, so referenced packages need an explicit build target as well.
(defn- build-spla-package [resource _dep-resources user-data]
  (with-open [out (ByteArrayOutputStream.)]
    (io/copy (io/input-stream (:source-resource user-data)) out)
    {:resource resource
     :content (.toByteArray out)}))

;; Reads a named entry from a ZipInputStream and returns it as text.
;; zip-stream is consumed while scanning, so callers use it for one targeted lookup only.
(defn- zip-entry-string [zip-stream entry-name]
  (loop [entry (.getNextEntry zip-stream)]
    (cond
      (nil? entry)
      nil

      (= entry-name (.getName entry))
      (slurp zip-stream)

      :else
      (recur (.getNextEntry zip-stream)))))

;; Reads every non-directory entry in a .spla zip resource into memory.
;; Returns nil on any exception so editor preview failures never block resource loading.
(defn- read-spla-entries [spla-resource]
  (try
    (when spla-resource
      (with-open [stream (java.util.zip.ZipInputStream. (io/input-stream spla-resource))]
        (loop [entry (.getNextEntry stream)
               entries {}]
          (if (nil? entry)
            entries
            (let [name (.getName entry)
                  data (when-not (.isDirectory entry)
                         (with-open [out (ByteArrayOutputStream.)]
                           (io/copy stream out)
                           (.toByteArray out)))]
              (recur (.getNextEntry stream)
                     (if data
                       (assoc entries name data)
                       entries)))))))
    (catch Throwable _
      nil)))

;; Builds the preview cache key from package path, package hash, and requested animation.
;; Including the sha1 makes the preview refresh when the .spla file changes on disk.
(defn- cache-key [package-resource default-animation]
  (when package-resource
    [(resource/resource->proj-path package-resource)
     (try
       (resource/resource->sha1-hex package-resource)
       (catch Throwable _
         nil))
     default-animation]))

;; Returns a cached preview image or stores a newly produced one.
;; cache-key is produced by cache-key and produce-fn must return a BufferedImage or nil.
(defn- cached-preview-image [cache-key produce-fn]
  (if-let [cached (get @preview-image-cache cache-key)]
    cached
    (let [preview (produce-fn)]
      (when preview
        (swap! preview-image-cache
               (fn [cache]
                 (let [cache (assoc cache cache-key preview)]
                   (if (<= (count cache) preview-image-cache-limit)
                     cache
                     (into {} (take-last preview-image-cache-limit cache)))))))
      preview)))

;; Coerces manifest numeric values to double with a fallback for missing/invalid values.
(defn- number-or [value fallback]
  (if (number? value)
    (double value)
    fallback))

;; Returns a positive integer dimension or nil.
(defn- positive-int [value]
  (when (and (number? value) (pos? (double value)))
    (int (Math/ceil (double value)))))

;; Checks the manifest shape supported by the static editor preview.
(defn- valid-spla-manifest? [manifest]
  (and (= "spla" (:format manifest))
       (= 1 (:version manifest))
       (map? (:canvas manifest))
       (sequential? (:parts manifest))
       (sequential? (:animations manifest))))

;; Reads and validates manifest.json from a .spla resource.
;; Returns nil on any error so editor UI falls back to plain fields/outline preview.
(defn- read-spla-manifest [spla-resource]
  (try
    (when spla-resource
      (with-open [stream (java.util.zip.ZipInputStream. (io/input-stream spla-resource))]
        (when-let [manifest-json (zip-entry-string stream "manifest.json")]
          (let [manifest (json/read-str manifest-json :key-fn keyword)]
            (when (valid-spla-manifest? manifest)
              manifest)))))
    (catch Throwable _
      nil)))

;; Extracts all animation ids from a parsed .spla manifest.
(defn- manifest-animation-ids [manifest]
  (->> (:animations manifest)
       (keep :id)
       (filter string?)
       distinct
       vec))

;; Selects the animation used for frame-0 preview.
;; default-animation wins when present; otherwise the first animation in the manifest is used.
(defn- select-preview-animation [manifest default-animation]
  (let [animations (:animations manifest)]
    (or (first (filter #(= default-animation (:id %)) animations))
        (first animations))))

;; Indexes manifest parts by id for quick frame-part lookup.
(defn- part-by-id [manifest]
  (into {}
        (map (fn [part]
               [(:id part) part]))
        (:parts manifest)))

;; Draws one frame part into the flattened Java2D preview image.
;; graphics is the destination Graphics2D, entries contains zip entry bytes, parts-by-id maps
;; part ids to manifest parts, and frame-part contains the frame-local transform.
(defn- draw-frame-part! [^java.awt.Graphics2D graphics entries parts-by-id frame-part]
  (when-let [part (get parts-by-id (:part frame-part))]
    (when-let [asset-bytes (get entries (:asset part))]
      (when-let [image (ImageIO/read (ByteArrayInputStream. asset-bytes))]
        (let [pivot (:pivot part)
              part-width (number-or (:width part) (.getWidth image))
              part-height (number-or (:height part) (.getHeight image))
              pivot-x (number-or (:x pivot) (* 0.5 part-width))
              pivot-y (number-or (:y pivot) (* 0.5 part-height))
              tx (number-or (:x frame-part) 0.0)
              ty (number-or (:y frame-part) 0.0)
              scale-x (number-or (:scaleX frame-part) 1.0)
              scale-y (number-or (:scaleY frame-part) 1.0)
              opacity (max 0.0 (min 1.0 (number-or (:opacity frame-part) 1.0)))
              radians (* (/ Math/PI 180.0) (- (number-or (:rotation frame-part) 0.0)))
              cos-r (Math/cos radians)
              sin-r (Math/sin radians)
              m00 (* cos-r scale-x)
              m01 (* sin-r scale-y)
              m02 (- tx (* cos-r scale-x pivot-x) (* sin-r scale-y pivot-y))
              m10 (* -1.0 sin-r scale-x)
              m11 (* cos-r scale-y)
              m12 (+ ty (* sin-r scale-x pivot-x) (* -1.0 cos-r scale-y pivot-y))
              transform (AffineTransform. m00 m10 m01 m11 m02 m12)
              old-composite (.getComposite graphics)]
          (.setComposite graphics (AlphaComposite/getInstance AlphaComposite/SRC_OVER (float opacity)))
          (.drawImage graphics image transform nil)
          (.setComposite graphics old-composite))))))

;; Composes one transparent BufferedImage for the selected animation's first frame.
;; manifest is parsed JSON data, entries are raw .spla zip entries, and default-animation is
;; the preferred animation id from the editor field.
(defn- compose-preview-image [manifest entries default-animation]
  (let [canvas (:canvas manifest)
        width (positive-int (:width canvas))
        height (positive-int (:height canvas))
        animation (select-preview-animation manifest default-animation)
        frame (first (:frames animation))]
    (when (and width height frame)
      (let [parts-by-id (part-by-id manifest)
            image (BufferedImage. width height BufferedImage/TYPE_INT_ARGB)]
        (let [graphics (.createGraphics image)]
          (try
            (.setRenderingHint graphics RenderingHints/KEY_INTERPOLATION RenderingHints/VALUE_INTERPOLATION_BILINEAR)
            (.setRenderingHint graphics RenderingHints/KEY_RENDERING RenderingHints/VALUE_RENDER_QUALITY)
            (doseq [frame-part (sort-by #(number-or (:drawOrder (get parts-by-id (:part %))) 0.0)
                                        (:parts frame))]
              (draw-frame-part! graphics entries parts-by-id frame-part))
            (finally
              (.dispose graphics))))
        image))))

;; Reads and composes the preview image for a .spla resource.
;; Any corrupt package, unsupported manifest, or image decode error returns nil for bounds-only
;; display instead of failing editor loading.
(defn- read-spla-preview-image [spla-resource default-animation]
  (when spla-resource
    (let [key (cache-key spla-resource default-animation)]
      (cached-preview-image
        key
        (fn []
          (try
            (when-let [entries (read-spla-entries spla-resource)]
              (when-let [manifest-bytes (get entries "manifest.json")]
                (let [manifest (json/read-str (String. ^bytes manifest-bytes "UTF-8") :key-fn keyword)]
                  (when (valid-spla-manifest? manifest)
                    (compose-preview-image manifest entries default-animation)))))
            (catch Throwable _
              nil)))))))

;; Reads the manifest canvas size without decoding PNG assets.
;; This keeps bounds available even when the static artwork preview cannot be produced.
(defn- read-spla-canvas-size [spla-resource]
  (try
    (when spla-resource
      (with-open [stream (java.util.zip.ZipInputStream. (io/input-stream spla-resource))]
        (when-let [manifest-json (zip-entry-string stream "manifest.json")]
          (let [manifest (json/read-str manifest-json :key-fn keyword)
                width (double (get-in manifest [:canvas :width]))
                height (double (get-in manifest [:canvas :height]))]
            (when (and (pos? width) (pos? height))
              [width height])))))
    (catch Throwable _
      nil)))

;; Graph output: canvas size for bounds, AABB, picking, and preview quad generation.
(g/defnk produce-canvas-size [package]
  (or (read-spla-canvas-size package)
      fallback-canvas-size))

;; Graph output: decoded static frame preview image, cached by package resource and animation.
(g/defnk produce-preview-image [package default-animation]
  (read-spla-preview-image package default-animation))

;; Graph output: animation ids used by the inspector Default Animation dropdown.
(g/defnk produce-animation-ids [package]
  (manifest-animation-ids (read-spla-manifest package)))

;; Graph output: options used by the standalone form view.
(g/defnk produce-animation-options [animation-ids]
  (mapv (fn [id] [id id]) animation-ids))

;; Graph output: Defold-editor-owned texture for the composed preview image.
;; _node-id is included in the texture id to keep component previews independent.
(g/defnk produce-preview-texture [_node-id preview-image]
  (when preview-image
    (texture/image-texture [::spriteloop-preview _node-id (.hashCode ^BufferedImage preview-image)]
                           preview-image
                           preview-texture-params)))

;; Produces line segments for the centered rectangle bounds and origin marker.
(defn- make-bounds-line-data [width height]
  (let [origin-size (min 32.0 (max 8.0 (* 0.04 (max width height))))
        half-width (* 0.5 width)
        half-height (* 0.5 height)
        min-x (- half-width)
        min-y (- half-height)
        max-x half-width
        max-y half-height]
    [[min-x min-y] [max-x min-y]
     [max-x min-y] [max-x max-y]
     [max-x max-y] [min-x max-y]
     [min-x max-y] [min-x min-y]
     [(- origin-size) 0.0] [origin-size 0.0]
     [0.0 (- origin-size)] [0.0 origin-size]]))

;; Emits a transformed colored line vertex for outline rendering.
(defn- gen-outline-vertex [^Matrix4d world-transform ^Point3d point x y cr cg cb]
  (.set point (double x) (double y) 0.0)
  (.transform world-transform point)
  (vector-of :float (.x point) (.y point) (.z point) cr cg cb 1.0))

;; Emits a transformed position-only vertex for editor picking.
(defn- gen-position-vertex [^Matrix4d world-transform ^Point3d point x y]
  (.set point (double x) (double y) 0.0)
  (.transform world-transform point)
  (vector-of :float (.x point) (.y point) (.z point)))

;; Emits a transformed textured vertex for the static preview quad.
(defn- gen-preview-vertex [^Matrix4d world-transform ^Point3d point x y u v]
  (.set point (double x) (double y) 0.0)
  (.transform world-transform point)
  (vector-of :float (.x point) (.y point) (.z point) u v 1.0 1.0 1.0 1.0))

;; Produces two triangles that cover the component bounds for picking.
(defn- make-bounds-triangle-data [width height]
  (let [half-width (* 0.5 width)
        half-height (* 0.5 height)
        min-x (- half-width)
        min-y (- half-height)
        max-x half-width
        max-y half-height]
    [[min-x min-y] [max-x min-y] [max-x max-y]
     [min-x min-y] [max-x max-y] [min-x max-y]]))

;; Produces two textured triangles for the preview image.
;; UVs match ImageIO's top-left image origin after Java2D composition.
(defn- make-preview-triangle-data [width height]
  (let [half-width (* 0.5 width)
        half-height (* 0.5 height)
        min-x (- half-width)
        min-y (- half-height)
        max-x half-width
        max-y half-height]
    [[min-x min-y 0.0 0.0] [max-x min-y 1.0 0.0] [max-x max-y 1.0 1.0]
     [min-x min-y 0.0 0.0] [max-x max-y 1.0 1.0] [min-x max-y 0.0 1.0]]))

;; Packs outline vertices for all batched SpriteLoop renderables.
(defn- gen-outline-vertex-buffer [renderables]
  (let [vertex-count (* 12 (count renderables))
        tmp-point (Point3d.)
        ^VertexBuffer vbuf (->color-vtx vertex-count)
        ^ByteBuffer buf (.buf vbuf)]
    (doseq [renderable renderables]
      (let [[cr cg cb] (colors/renderable-outline-color renderable)
            ^Matrix4d world-transform (:world-transform renderable)
            {:keys [width height]} (:user-data renderable)]
        (doseq [[x y] (make-bounds-line-data width height)]
          (vtx/buf-push-floats! buf (gen-outline-vertex world-transform tmp-point x y cr cg cb)))))
    (vtx/flip! vbuf)))

;; Packs one picking quad for the selected renderable.
(defn- gen-pick-vertex-buffer [renderable]
  (let [tmp-point (Point3d.)
        ^VertexBuffer vbuf (->position-vtx 6)
        ^ByteBuffer buf (.buf vbuf)
        ^Matrix4d world-transform (:world-transform renderable)
        {:keys [width height]} (:user-data renderable)]
    (doseq [[x y] (make-bounds-triangle-data width height)]
      (vtx/buf-push-floats! buf (gen-position-vertex world-transform tmp-point x y)))
    (vtx/flip! vbuf)))

;; Packs one textured quad for the static preview renderable.
(defn- gen-preview-vertex-buffer [renderable]
  (let [tmp-point (Point3d.)
        ^VertexBuffer vbuf (->preview-vtx 6)
        ^ByteBuffer buf (.buf vbuf)
        ^Matrix4d world-transform (:world-transform renderable)
        {:keys [width height]} (:user-data renderable)]
    (doseq [[x y u v] (make-preview-triangle-data width height)]
      (vtx/buf-push-floats! buf (gen-preview-vertex world-transform tmp-point x y u v)))
    (vtx/flip! vbuf)))

;; Draws the flattened preview texture during the transparent scene pass.
(defn- render-spriteloop-preview [^GL2 gl render-args renderables _count]
  (assert (= pass/transparent (:pass render-args)))
  (gl/set-blend-mode gl :blend-mode-alpha)
  (doseq [renderable renderables]
    (let [gpu-texture (:gpu-texture (:user-data renderable))
          preview-vertex-binding (vtx/use-with ::spriteloop-preview (gen-preview-vertex-buffer renderable) preview-shader)]
      (when gpu-texture
        (gl/with-gl-bindings gl render-args [preview-shader gpu-texture preview-vertex-binding]
          (gl/gl-draw-arrays gl GL/GL_TRIANGLES 0 6))))))

;; Draws the bounds rectangle and origin marker during the outline scene pass.
(defn- render-spriteloop-outline [^GL2 gl render-args renderables _count]
  (assert (= pass/outline (:pass render-args)))
  (let [vertex-count (* 12 (count renderables))
        outline-vertex-binding (vtx/use-with ::spriteloop-outline (gen-outline-vertex-buffer renderables) outline-shader)]
    (gl/with-gl-bindings gl render-args [outline-shader outline-vertex-binding]
      (gl/gl-draw-arrays gl GL/GL_LINES 0 vertex-count))))

;; Draws the invisible selection quad used by editor picking.
(defn- render-spriteloop-selection [^GL2 gl render-args renderables _count]
  (assert (= pass/selection (:pass render-args)))
  (doseq [renderable renderables]
    (let [pick-vertex-binding (vtx/use-with ::spriteloop-pick (gen-pick-vertex-buffer renderable) pick-shader)
          render-args (assoc render-args :id (scene-picking/renderable-picking-id-uniform renderable))]
      (gl/with-gl-bindings gl render-args [pick-shader pick-vertex-binding]
        (gl/gl-draw-arrays gl GL/GL_TRIANGLES 0 6)))))

;; Graph output: centered AABB for editor selection and framing.
(g/defnk produce-aabb [canvas-size]
  (let [[^double width ^double height] canvas-size]
    (geom/make-aabb (Point3d. (* -0.5 width) (* -0.5 height) -0.5)
                    (Point3d. (* 0.5 width) (* 0.5 height) 0.5))))

;; Graph output: scene renderables for picking, optional preview, and outline overlay.
;; The editor applies normal game object/component transforms to these renderables.
(g/defnk produce-scene [_node-id aabb canvas-size preview-texture visible]
  (let [[width height] canvas-size
        show-preview? (and preview-texture (not (false? visible)))]
    {:node-id _node-id
     :aabb aabb
     :renderable {:render-fn render-spriteloop-selection
                  :batch-key [pick-shader]
                  :select-batch-key _node-id
                  :tags #{:spriteloop}
                  :user-data {:width width
                              :height height}
                  :passes [pass/selection]}
     :children (cond-> []
                 show-preview?
                 (conj {:node-id _node-id
                        :aabb aabb
                        :renderable {:render-fn render-spriteloop-preview
                                     :batch-key [preview-shader preview-texture]
                                     :tags #{:spriteloop :preview}
                                     :user-data {:width width
                                                 :height height
                                                 :gpu-texture preview-texture}
                                     :passes [pass/transparent]}})
                 true
                 (conj {:node-id _node-id
                        :aabb aabb
                        :renderable {:render-fn render-spriteloop-outline
                                     :batch-key [outline-shader]
                                     :tags #{:spriteloop :outline}
                                     :select-batch-key _node-id
                                     :user-data {:width width
                                                 :height height}
                                     :passes [pass/outline]}}))}))

;; Graph output: build targets for the compiled .spriteloopc resource and its raw .spla package.
(g/defnk produce-build-targets [_node-id resource package save-value own-build-errors material-resource dep-build-targets]
  (g/precluding-errors own-build-errors
    (let [dep-build-targets (flatten dep-build-targets)
          deps-by-source (into {}
                               (map #(let [res (:resource %)]
                                       [(:resource res) res]))
                               dep-build-targets)
          dep-resources (map (fn [[label resource]]
                               [label (get deps-by-source resource)])
                             [[:material material-resource]])
          package-build-target (when package
                                 (bt/with-content-hash
                                   {:node-id _node-id
                                    :resource (workspace/make-build-resource package)
                                    :build-fn build-spla-package
                                    :user-data {:source-resource package}}))]
      (cond-> [(bt/with-content-hash
                 {:node-id _node-id
                  :resource (workspace/make-build-resource resource)
                  :build-fn build-spriteloop
                  :user-data {:spriteloop-pb save-value
                              :dep-resources dep-resources}
                  :deps (cond-> dep-build-targets
                          package-build-target
                          (conj package-build-target))})]
        package-build-target
        (conj package-build-target)))))

;; Inspector set operation for one editable property.
;; user-data carries the node/resource, property-path contains the edited field, and value is
;; normalized before writing to the graph.
(defn- set-form-op [user-data property-path value]
  (assert (= 1 (count property-path)))
  (let [prop (first property-path)
        value (if (#{:package :material} prop)
                (workspace/resolve-resource (:resource user-data)
                                            (cond
                                              (seq value) value
                                              (= :material prop) default-material
                                              :else nil))
                value)]
    (g/set-property (:node-id user-data) prop value)))

;; Inspector clear operation for one editable property.
;; Material clears back to the adapter default; other fields clear to graph defaults.
(defn- clear-form-op [user-data property-path]
  (assert (= 1 (count property-path)))
  (let [prop (first property-path)]
    (if (= :material prop)
      (g/set-property (:node-id user-data) prop
                      (workspace/resolve-resource (:resource user-data) default-material))
      (g/clear-property (:node-id user-data) prop))))

;; Graph output: Clojure form model shown in the Defold inspector.
(g/defnk produce-form-data [_node-id resource package default-animation playback-rate loop visible autoplay material animation-options]
  (let [default-animation-field (cond-> {:path [:default-animation]
                                         :label "Default Animation"
                                         :type :string}
                                  (seq animation-options)
                                  (assoc :type :choicebox
                                         :options animation-options
                                         :from-string identity))]
    {:navigation false
     :form-ops {:user-data {:node-id _node-id
                            :resource resource}
                :set set-form-op
                :clear clear-form-op}
     :sections [{:title "SpriteLoop"
                 :fields [{:path [:package]
                           :label "Package"
                           :type :string}
                          default-animation-field
                          {:path [:playback-rate]
                           :label "Playback Rate"
                           :type :number}
                          {:path [:loop]
                           :label "Loop"
                           :type :boolean}
                          {:path [:visible]
                           :label "Visible"
                           :type :boolean}
                          {:path [:autoplay]
                           :label "Autoplay"
                           :type :boolean}
                          {:path [:material]
                           :label "Material"
                           :type :string}]}]
     :values {[:package] (resource/resource->proj-path package)
              [:default-animation] default-animation
              [:playback-rate] playback-rate
              [:loop] loop
              [:visible] visible
              [:autoplay] autoplay
              [:material] (or (resource/resource->proj-path material) default-material)}}))

;; Graph output: serializable DDF map saved into the .spriteloop file.
(g/defnk produce-save-value [package default-animation playback-rate loop visible autoplay material]
  (protobuf/make-map-without-defaults @spriteloop-desc-cls
    :package (resource/resource->proj-path package)
    :default-animation default-animation
    :playback-rate playback-rate
    :loop loop
    :visible visible
    :autoplay autoplay
    :material (or (resource/resource->proj-path material) default-material)))

;; Graph output: validation errors that should block builds.
(g/defnk produce-own-build-errors [_node-id package material]
  (g/package-errors
    _node-id
    (validate-package _node-id package)
    (validate-material _node-id material)))

;; Loads a .spriteloop DDF file into graph node properties.
;; _project is unused, self is the node id, _resource resolves referenced resources, and data is
;; the protobuf map read by the editor.
(defn- load-spriteloop [_project self _resource data]
  (let [resolve-resource #(workspace/resolve-resource _resource %)]
    (gu/set-properties-from-pb-map self @spriteloop-desc-cls data
      package (resolve-resource (:package :or ""))
      default-animation :default-animation
      playback-rate :playback-rate
      loop :loop
      visible :visible
      autoplay :autoplay
      material (resolve-resource (:material :or default-material)))))

;; Editor graph node for one .spriteloop resource.
;; Properties become inspector fields, outputs feed the scene, save data, and Bob build graph.
(g/defnode SpriteLoopNode
  (inherits resource-node/ResourceNode)

  (property package resource/Resource
            (dynamic edit-type (g/constantly {:type resource/Resource :ext "spla"}))
            (dynamic error (g/fnk [_node-id package] (validate-package _node-id package))))
  (property default-animation g/Str
            (default "idle")
            (dynamic edit-type (g/fnk [animation-ids]
                                 (if (seq animation-ids)
                                   (properties/->choicebox animation-ids)
                                   {:type g/Str})))
            (dynamic ext-edit-type (g/constantly {:type g/Str})))
  (property playback-rate g/Num
            (default 1.0))
  (property loop g/Bool
            (default true))
  (property visible g/Bool
            (default true))
  (property autoplay g/Bool
            (default true))
  (property material resource/Resource
            (value (gu/passthrough material-resource))
            (set (fn [evaluation-context self old-value new-value]
                   (project/resource-setter evaluation-context self old-value new-value
                                            [:resource :material-resource]
                                            [:build-targets :dep-build-targets])))
            (dynamic edit-type (g/constantly {:type resource/Resource :ext "material"}))
            (dynamic error (g/fnk [_node-id material]
                             (validate-material _node-id material))))

  (input material-resource resource/Resource)
  (input dep-build-targets g/Any :array)

  (output animation-ids g/Any :cached produce-animation-ids)
  (output animation-options g/Any :cached produce-animation-options)
  (output form-data g/Any :cached produce-form-data)
  (output canvas-size g/Any :cached produce-canvas-size)
  (output preview-image g/Any :cached produce-preview-image)
  (output preview-texture g/Any :cached produce-preview-texture)
  (output aabb AABB :cached produce-aabb)
  (output scene g/Any :cached produce-scene)
  (output save-value g/Any :cached produce-save-value)
  (output own-build-errors g/Any produce-own-build-errors)
  (output build-targets g/Any :cached produce-build-targets))

;; Registers the .spriteloop resource type with the Defold editor workspace.
(defn- register-resource-types [workspace]
  (when-let [desc-cls @spriteloop-desc-cls]
    (concat
      (workspace/register-resource-type
        workspace
        :ext spla-ext
        :build-ext spla-ext
        :label "SpriteLoop Package"
        :view-types [:default])
      (resource-node/register-ddf-resource-type
        workspace
        :ext spriteloop-ext
        :label "SpriteLoop"
        :node-type SpriteLoopNode
        :ddf-type desc-cls
        :load-fn load-spriteloop
        :icon spriteloop-icon
        :category (localization/message "resource.category.components")
        :view-types [:scene :cljfx-form-view :text]
        :view-opts {}
        :tags #{:component}
        :tag-opts {:component {:transform-properties #{:position :rotation :scale}}}
        :template spriteloop-template))))

;; Plugin load hook body. Defold calls the returned function with the workspace.
(defn- load-plugin-spriteloop [workspace]
  (when-let [tx-data (register-resource-types workspace)]
    (g/transact tx-data)))

;; Returns the editor plugin entry function consumed by Defold's plugin loader.
(defn- return-plugin []
  (fn [workspace]
    (load-plugin-spriteloop workspace)))

(return-plugin)
