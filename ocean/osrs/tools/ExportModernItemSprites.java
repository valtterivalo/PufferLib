import java.awt.image.BufferedImage;
import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import javax.imageio.ImageIO;
import net.runelite.cache.IndexType;
import net.runelite.cache.definitions.ItemDefinition;
import net.runelite.cache.definitions.ModelDefinition;
import net.runelite.cache.definitions.SpriteDefinition;
import net.runelite.cache.definitions.TextureDefinition;
import net.runelite.cache.definitions.loaders.ModelLoader;
import net.runelite.cache.definitions.loaders.SpriteLoader;
import net.runelite.cache.definitions.providers.ItemProvider;
import net.runelite.cache.definitions.providers.ModelProvider;
import net.runelite.cache.definitions.providers.SpriteProvider;
import net.runelite.cache.definitions.providers.TextureProvider;
import net.runelite.cache.fs.Archive;
import net.runelite.cache.fs.Index;
import net.runelite.cache.fs.Store;
import net.runelite.cache.item.ItemSpriteFactory;

public final class ExportModernItemSprites {
    private ExportModernItemSprites() {
    }

    public static void main(String[] args) throws Exception {
        Args parsed = Args.parse(args);
        Map<Integer, ItemDefinition> items = readItems(parsed.itemsTsv);
        TextureDefinition[] textures = readTextures(parsed.texturesTsv);
        List<Integer> ids = parseIds(parsed.ids);

        Files.createDirectories(parsed.outputDir);
        try (Store store = new Store(parsed.cacheDir.toFile())) {
            store.load();
            CacheModelProvider modelProvider = new CacheModelProvider(store);
            CacheSpriteProvider spriteProvider = new CacheSpriteProvider(store);
            ItemProvider itemProvider = id -> {
                ItemDefinition item = items.get(id);
                if (item == null) {
                    throw new IllegalArgumentException("item definition missing for " + id);
                }
                return item;
            };
            TextureProvider textureProvider = () -> textures;

            for (int id : ids) {
                BufferedImage image = ItemSpriteFactory.createSprite(
                    itemProvider,
                    modelProvider,
                    spriteProvider,
                    textureProvider,
                    id,
                    1,
                    1,
                    3158064,
                    false
                );
                if (image == null) {
                    throw new IOException("sprite renderer returned null for item " + id);
                }
                Path output = parsed.outputDir.resolve(id + ".png");
                ImageIO.write(image, "png", output.toFile());
                if (!Files.isRegularFile(output) || Files.size(output) == 0) {
                    throw new IOException("sprite output empty for item " + id);
                }
                System.out.println("sprite " + id + " -> " + output);
            }
        }
    }

    private static Map<Integer, ItemDefinition> readItems(Path path) throws IOException {
        Map<Integer, ItemDefinition> out = new HashMap<>();
        try (BufferedReader reader = Files.newBufferedReader(path, StandardCharsets.UTF_8)) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.isEmpty() || line.startsWith("item_id\t")) {
                    continue;
                }
                String[] parts = line.split("\t", -1);
                if (parts.length != 27) {
                    throw new IOException("bad item TSV field count " + parts.length + " in " + line);
                }
                int pos = 0;
                int id = parseInt(parts[pos++]);
                ItemDefinition item = new ItemDefinition(id);
                item.name = parts[pos++];
                item.inventoryModel = parseInt(parts[pos++]);
                item.zoom2d = parseInt(parts[pos++]);
                item.xan2d = parseInt(parts[pos++]);
                item.yan2d = parseInt(parts[pos++]);
                item.zan2d = parseInt(parts[pos++]);
                item.xOffset2d = parseInt(parts[pos++]);
                item.yOffset2d = parseInt(parts[pos++]);
                item.resizeX = parseInt(parts[pos++]);
                item.resizeY = parseInt(parts[pos++]);
                item.resizeZ = parseInt(parts[pos++]);
                item.ambient = parseInt(parts[pos++]);
                item.contrast = parseInt(parts[pos++]);
                item.stackable = parseInt(parts[pos++]);
                item.notedID = parseInt(parts[pos++]);
                item.notedTemplate = parseInt(parts[pos++]);
                item.boughtId = parseInt(parts[pos++]);
                item.boughtTemplateId = parseInt(parts[pos++]);
                item.placeholderId = parseInt(parts[pos++]);
                item.placeholderTemplateId = parseInt(parts[pos++]);
                item.colorFind = parseShortArray(parts[pos++]);
                item.colorReplace = parseShortArray(parts[pos++]);
                item.textureFind = parseShortArray(parts[pos++]);
                item.textureReplace = parseShortArray(parts[pos++]);
                item.countObj = parseIntArray(parts[pos++]);
                item.countCo = parseIntArray(parts[pos++]);
                item.params = null;
                out.put(id, item);
            }
        }
        return out;
    }

    private static TextureDefinition[] readTextures(Path path) throws IOException {
        List<TextureDefinition> out = new ArrayList<>();
        try (BufferedReader reader = Files.newBufferedReader(path, StandardCharsets.UTF_8)) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.isEmpty() || line.startsWith("texture_id\t")) {
                    continue;
                }
                String[] parts = line.split("\t", -1);
                if (parts.length != 6) {
                    throw new IOException("bad texture TSV field count " + parts.length + " in " + line);
                }
                TextureDefinition texture = new TextureDefinition();
                texture.setId(parseInt(parts[0]));
                texture.field1777 = parseInt(parts[2]);
                texture.field1778 = parseInt(parts[3]) != 0;
                texture.setFileIds(new int[] { parseInt(parts[1]) });
                texture.setField1786(new int[] { 0 });
                texture.animationDirection = parseInt(parts[4]);
                texture.animationSpeed = parseInt(parts[5]);
                out.add(texture);
            }
        }
        return out.toArray(new TextureDefinition[0]);
    }

    private static List<Integer> parseIds(String raw) {
        List<Integer> out = new ArrayList<>();
        for (String part : raw.split(",")) {
            if (!part.isBlank()) {
                out.add(Integer.parseInt(part.trim()));
            }
        }
        if (out.isEmpty()) {
            throw new IllegalArgumentException("no item ids requested");
        }
        return out;
    }

    private static short[] parseShortArray(String raw) {
        if (raw.isEmpty()) {
            return null;
        }
        String[] parts = raw.split(",");
        short[] out = new short[parts.length];
        for (int i = 0; i < parts.length; i++) {
            out[i] = (short) Integer.parseInt(parts[i]);
        }
        return out;
    }

    private static int[] parseIntArray(String raw) {
        if (raw.isEmpty()) {
            return null;
        }
        String[] parts = raw.split(",");
        int[] out = new int[parts.length];
        for (int i = 0; i < parts.length; i++) {
            out[i] = Integer.parseInt(parts[i]);
        }
        return out;
    }

    private static int parseInt(String raw) {
        return Integer.parseInt(raw);
    }

    private static final class CacheModelProvider implements ModelProvider {
        private final Store store;
        private final Index index;
        private final ModelLoader loader = new ModelLoader();
        private final Map<Integer, ModelDefinition> cache = new HashMap<>();

        CacheModelProvider(Store store) {
            this.store = store;
            this.index = store.getIndex(IndexType.MODELS);
            if (this.index == null) {
                throw new IllegalStateException("model index missing");
            }
        }

        @Override
        public ModelDefinition provide(int modelId) throws IOException {
            ModelDefinition cached = cache.get(modelId);
            if (cached != null) {
                return cached;
            }
            Archive archive = index.getArchive(modelId);
            if (archive == null) {
                throw new IOException("model " + modelId + " missing from cache");
            }
            byte[] raw = store.getStorage().loadArchive(archive);
            byte[] data = archive.decompress(raw);
            ModelDefinition model = loader.load(modelId, data);
            if (model == null) {
                throw new IOException("model " + modelId + " failed to decode");
            }
            cache.put(modelId, model);
            return model;
        }
    }

    private static final class CacheSpriteProvider implements SpriteProvider {
        private final Store store;
        private final Index index;
        private final SpriteLoader loader = new SpriteLoader();
        private final Map<Long, SpriteDefinition> frames = new HashMap<>();
        private final Set<Integer> loadedGroups = new HashSet<>();

        CacheSpriteProvider(Store store) {
            this.store = store;
            this.index = store.getIndex(IndexType.SPRITES);
            if (this.index == null) {
                throw new IllegalStateException("sprite index missing");
            }
        }

        @Override
        public SpriteDefinition provide(int spriteId, int frame) {
            long key = (((long) spriteId) << 32) | (frame & 0xFFFFFFFFL);
            SpriteDefinition cached = frames.get(key);
            if (cached != null) {
                return cached;
            }
            if (!loadedGroups.contains(spriteId)) {
                loadGroup(spriteId);
            }
            return frames.get(key);
        }

        private void loadGroup(int spriteId) {
            loadedGroups.add(spriteId);
            Archive archive = index.getArchive(spriteId);
            if (archive == null) {
                return;
            }
            try {
                byte[] raw = store.getStorage().loadArchive(archive);
                byte[] data = archive.decompress(raw);
                SpriteDefinition[] sprites = loader.load(spriteId, data);
                for (SpriteDefinition sprite : sprites) {
                    long key = (((long) spriteId) << 32) | (sprite.getFrame() & 0xFFFFFFFFL);
                    frames.put(key, sprite);
                }
            } catch (IOException ex) {
                throw new IllegalStateException("sprite group " + spriteId + " failed to decode", ex);
            }
        }
    }

    private static final class Args {
        final Path cacheDir;
        final Path outputDir;
        final Path itemsTsv;
        final Path texturesTsv;
        final String ids;

        Args(Path cacheDir, Path outputDir, Path itemsTsv, Path texturesTsv, String ids) {
            this.cacheDir = cacheDir;
            this.outputDir = outputDir;
            this.itemsTsv = itemsTsv;
            this.texturesTsv = texturesTsv;
            this.ids = ids;
        }

        static Args parse(String[] args) {
            Map<String, String> values = new HashMap<>();
            for (int i = 0; i < args.length; i += 2) {
                if (i + 1 >= args.length) {
                    throw new IllegalArgumentException("missing value for " + args[i]);
                }
                values.put(args[i], args[i + 1]);
            }
            return new Args(
                Path.of(required(values, "--cache")),
                Path.of(required(values, "--output")),
                Path.of(required(values, "--items-tsv")),
                Path.of(required(values, "--textures-tsv")),
                required(values, "--ids")
            );
        }

        private static String required(Map<String, String> values, String key) {
            String value = values.get(key);
            if (value == null || value.isEmpty()) {
                throw new IllegalArgumentException("missing " + key);
            }
            return value;
        }
    }
}
