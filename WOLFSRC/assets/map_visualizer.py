import re
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap
from matplotlib.widgets import Button
from collections import Counter

MAP_SIZE = 64


# ============================================================
# Paste your C map here OR load from file
# ============================================================

with open("map_e1m1.h", "r", encoding="utf-8") as f:
    C_SOURCE = f.read()


# ============================================================
# Parse map array from C source
# ============================================================

def parse_map(source):
    numbers = list(map(int, re.findall(r'\b\d+\b', source)))

    # Find the actual map data (4096 entries)
    if len(numbers) < MAP_SIZE * MAP_SIZE:
        raise ValueError("Not enough map data found")

    map_data = numbers[-(MAP_SIZE * MAP_SIZE):]

    if len(map_data) != 4096:
        raise ValueError(
            f"Expected 4096 tiles, got {len(map_data)}"
        )

    return np.array(map_data, dtype=np.uint8).reshape(
        (MAP_SIZE, MAP_SIZE)
    )


grid = parse_map(C_SOURCE)


# ============================================================
# Tile colors
# Customize however you want
# ============================================================

unique_tiles = sorted(np.unique(grid))

rng = np.random.default_rng(0)

tile_to_color_index = {
    tile: i for i, tile in enumerate(unique_tiles)
}

colors = []

for tile in unique_tiles:
    if tile == 1:
        colors.append((0.1, 0.1, 0.1))      # walls
    elif tile in [8, 9]:
        colors.append((0.15, 0.25, 0.65))  # outdoor
    elif tile in [12]:
        colors.append((0.55, 0.45, 0.25))  # stone floor
    elif tile in [110, 111]:
        colors.append((0.3, 0.7, 0.3))
    elif tile >= 140:
        colors.append((0.8, 0.2, 0.2))
    else:
        colors.append(rng.random(3) * 0.8 + 0.2)

mapped_grid = np.vectorize(tile_to_color_index.get)(grid)
cmap = ListedColormap(colors)


# ============================================================
# Plot setup
# ============================================================

fig, ax = plt.subplots(figsize=(14, 14))
plt.subplots_adjust(bottom=0.12)

img = ax.imshow(
    mapped_grid,
    cmap=cmap,
    interpolation="nearest"
)

ax.set_title("Wolf3D Level Planner")
ax.set_xlabel("X")
ax.set_ylabel("Y")

ax.set_xticks(np.arange(MAP_SIZE))
ax.set_yticks(np.arange(MAP_SIZE))

ax.set_xticks(np.arange(-0.5, MAP_SIZE, 1), minor=True)
ax.set_yticks(np.arange(-0.5, MAP_SIZE, 1), minor=True)

ax.grid(which="minor", color="white", linewidth=0.3)

ax.set_xlim(-0.5, MAP_SIZE - 0.5)
ax.set_ylim(MAP_SIZE - 0.5, -0.5)


# ============================================================
# Tile labels toggle
# ============================================================

show_numbers = True
text_labels = []

def draw_labels():
    global text_labels

    for t in text_labels:
        t.remove()

    text_labels = []

    if not show_numbers:
        fig.canvas.draw_idle()
        return

    for y in range(MAP_SIZE):
        for x in range(MAP_SIZE):
            tile = grid[y, x]

            txt = ax.text(
                x,
                y,
                str(tile),
                ha="center",
                va="center",
                fontsize=6,
                color="white"
            )

            text_labels.append(txt)

    fig.canvas.draw_idle()


draw_labels()


# ============================================================
# Tile inspector
# ============================================================

selected_text = ax.text(
    0.01,
    1.02,
    "",
    transform=ax.transAxes,
    fontsize=12
)

cursor_marker, = ax.plot(
    [],
    [],
    marker='s',
    markersize=12,
    markerfacecolor='none',
    markeredgecolor='yellow',
    linewidth=2
)


def on_click(event):
    if event.inaxes != ax:
        return

    x = int(round(event.xdata))
    y = int(round(event.ydata))

    if 0 <= x < MAP_SIZE and 0 <= y < MAP_SIZE:
        tile = grid[y, x]

        selected_text.set_text(
            f"Tile ({x}, {y}) = {tile}"
        )

        cursor_marker.set_data([x], [y])

        print(
            f"Tile ({x}, {y}) -> {tile}"
        )

        fig.canvas.draw_idle()


fig.canvas.mpl_connect(
    'button_press_event',
    on_click
)


# ============================================================
# Toggle labels button
# ============================================================

ax_button = plt.axes([0.8, 0.02, 0.15, 0.05])
btn = Button(ax_button, "Toggle IDs")


def toggle_labels(event):
    global show_numbers
    show_numbers = not show_numbers
    draw_labels()


btn.on_clicked(toggle_labels)


# ============================================================
# Tile statistics
# ============================================================

counts = Counter(grid.flatten())

print("\nTile Statistics")
print("-" * 30)

for tile, count in sorted(counts.items()):
    print(f"Tile {tile:3d}: {count:4d}")


plt.show()
