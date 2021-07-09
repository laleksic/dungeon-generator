#include <vector>
#include <algorithm>
#include <functional>
#include <cstring>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include "BearLibTerminal.h"

enum {
    tk_floor,
    tk_wall,
    tk_conn,
    tk_cull
};

struct tile_t {
    char region;
    char room;
    char kind;
    bool door;
};

struct room_t {
    char x0,y0,x1,y1;
};

struct xy_t { int x, y; };
struct range_t { int lo, hi; };

static const int width = 79;
static const int height = 25;
static const int max_rooms = 16;

static const range_t room_width{7, 10};
static const range_t room_height{5, 7};

static room_t rooms[max_rooms];
static int n_rooms = 0;
static int next_region = 0;

static tile_t tiles[width][height];

static bool animate_make_connections = true;
static bool animate_make_maze = true;
static bool animate_make_rooms = true;
static bool animate_remove_dead_ends = true;

void benchmark(const char *name, std::function<void()> fn) {
    clock_t bench = clock();
    fn();
    clock_t diff = clock()-bench;
    double secs = (double)diff / CLOCKS_PER_SEC;
    printf("%s took %lf seconds\n", name, secs);
}


int randrange(range_t r) {
    return r.lo + rand()%(r.hi-r.lo+1);
}

template <typename T>
struct weighted_selector_t {
    std::vector<T> items;
    std::vector<int> weights;
    int weight_sum = 0;
    
    void push_back(T item, int weight) {
        items.push_back(item);
        weights.push_back(weight);
        weight_sum += weight;
    }   
    
    T select() {
        int r = randrange(range_t{0, weight_sum});
        for (int i=0; i<items.size(); ++i) {
            r -= weights[i];
            if (r <= 0) {
                return items[i];
            }
        }
        assert(false);
    }
    
    bool empty() {
        return items.empty();
    }
};

float construct_float(uint32_t sign_bit, uint32_t exponent, uint32_t mantissa) {
    uint32_t bits = 0b00000000'00000000'00000000'00000000;
    
    mantissa    &= 0b00000000'01111111'11111111'11111111;
    exponent   <<= 23;
    exponent    &= 0b01111111'10000000'00000000'00000000;
    sign_bit   <<= 23+8;
    sign_bit    &= 0b10000000'00000000'00000000'00000000;
    
    // float = +- (sign bit) 2^(127-exponent) * 1.mantissa
    
    bits |= mantissa;
    bits |= exponent;
    bits |= sign_bit;

    float f;
    memcpy(&f, &bits, sizeof(bits));
    return f;
}

float random_float(uint32_t random_int) {
    // random float in range [1, 2)
    float x = construct_float(0, 127, random_int);    
    return x - 1.0f;
}

// https://stackoverflow.com/a/12996028
uint32_t hash(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

// https://www.rapidtables.com/convert/color/hsv-to-rgb.html
void hsv2rgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    float c = v*s;
    float x = c * (1-fabsf(fmodf(h/60.0f, 2.0f)-1.0f));
    float m = v-c;
    float r_, g_, b_;
    if      (0.0f <= h && h < 60.0f) {
        r_ = c;  g_ = x;  b_ = 0.0f;
    }
    else if (60.0f <= h && h < 120.0f) {
        r_ = x;  g_ = c;  b_ = 0.0f;
    }
    else if (120.0f <= h && h < 180.0f) {
        r_ = 0.0f;  g_ = c;  b_ = x;
    }
    else if (180.0f <= h && h < 240.0f) {
        r_ = 0.0f;  g_ = x;  b_ = c;
    }
    else if (240.0f <= h && h < 300.0f) {
        r_ = x;  g_ = 0.0f;  b_ = c;
    }
    else if (300.0f <= h && h <= 360.0f) {
        r_ = c;  g_ = 0.0f;  b_ = x;    
    }
    r = (r_+m)*0xff;
    g = (g_+m)*0xff;
    b = (b_+m)*0xff;
}

color_t regioncolor(int region) {
    uint32_t hashed = hash(region);
    uint8_t hb, sb, vb;
    hb = hashed & 0xff;
    sb = (hashed>>8) & 0xff;
    vb = (hashed>>8) & 0xff;
    float h = hb/255.0f * 360.0f;
    float s = 0.25f + (sb/255.0f * 0.5f);
    float v = 0.25f + (vb/255.0f * 0.5f);
    
    uint8_t r,g,b;
    hsv2rgb(h,s,v,r,g,b);
    
    return color_from_argb(0xff, r, g, b);
}

void hilite_rect(int x0, int y0, int x1, int y1, color_t color) {
    terminal_bkcolor(color);
    for (int x=x0; x<=x1; x+=1)
    for (int y=y0; y<=y1; y+=1) {
        terminal_put(x, y, ' ');
    }
}

void hilite_tile(int x, int y, color_t color) {
    terminal_bkcolor(color);
    terminal_put(x, y, ' ');
}

void display(bool show_regions=false, bool ascii=false) {
    terminal_clear();
    for (int x=0; x<width; x+=1)
    for (int y=0; y<height; y+=1) {
        color_t fg, bk;
        char ch;
        tile_t t = tiles[x][y];
        
        switch(t.kind) {
            case tk_floor:
                ch = ' ';//'.';
                fg = color_from_name("black");
                bk = color_from_name("light yellow");
                
                break;
            case tk_wall:
                ch = ' ';//'#';
                fg = color_from_name("white");
                bk = color_from_name("dark blue");
                break;
            case tk_conn:
                ch = ' ';//'+';
                fg = color_from_name("white");
                bk = color_from_name("black");
                break;
            case tk_cull:
                ch = ' ';//'x';
                fg = color_from_name("white");
                bk = color_from_name("black");
                break;
            default:
                assert(0);
        }
        
        
        // if (t.room >= 0)
            // ch = 'a'+t.room;
            
        if (show_regions) {
            ch = ' ';
            if (t.region == 0) {
                bk = color_from_name("light yellow");
                fg = bk;
            }
            else if (t.region > 0) {
                bk = regioncolor(t.region);
                fg = bk;
            } else {
                bk = color_from_name("dark blue");
                fg = bk;
            }
        }
        
        if (ascii) {
            fg = color_from_name("white");
            bk = color_from_name("black");        
            
            if (t.door) {
                ch = '+';
                fg = color_from_name("white");
                bk = color_from_name("dark blue");
            }
        }
        
        
        
        
        terminal_color(fg);
        terminal_bkcolor(bk);
        terminal_put(x, y, ch);
    }
}

void delay(int msecs) {
    terminal_refresh();
    terminal_delay(msecs);
}

void wait_for_input() {
    terminal_refresh();
    int input = terminal_read();
    if (input == TK_CLOSE)
        exit(1);
}

void init() {
    for (int x=0; x<width; x+=1)
    for (int y=0; y<height; y+=1) {
        tile_t& t = tiles[x][y];
        t.region  = -1;
        t.room    = -1;
        t.kind    = tk_wall;
        t.door    = false;
    }
    
    n_rooms     = 0;
    next_region = 0;
}

void make_rooms() {
    int tries = 0;
    static const int max_tries = 200;
   
    while (tries < max_tries && n_rooms < max_rooms) {
        room_t r;
        int w,h;
        do { w = randrange(room_width);  } while (w%2 == 0);
        do { h = randrange(room_height); } while (h%2 == 0);
        
        do { r.x0 = randrange(range_t{0, width-w});  } while (r.x0%2);
        do { r.y0 = randrange(range_t{0, height-h}); } while (r.y0%2);
        
        r.x1 = r.x0+w-1;
        r.y1 = r.y0+h-1;
        
        assert(r.x1%2 == 0);
        assert(r.y1%2 == 0);
        
        
        for (int x=r.x0; x<=r.x1; x+=1)
        for (int y=r.y0; y<=r.y1; y+=1) {
            if (tiles[x][y].kind == tk_floor) {
                /*if (animate_make_rooms) {
                    display();
                    hilite_rect(r.x0, r.y0, r.x1, r.y1, color_from_name("red"));
                    delay(1);
                }*/
                
                tries += 1;
                goto try_again;
            }
        }
        
        
        rooms[n_rooms] = r;
        
        for (int x=r.x0; x<=r.x1; x+=1)
        for (int y=r.y0; y<=r.y1; y+=1) {
            tiles[x][y].room = n_rooms;
        }
        
        for (int x=r.x0+1; x<=r.x1-1; x+=1)
        for (int y=r.y0+1; y<=r.y1-1; y+=1) {
            tiles[x][y].kind = tk_floor;
            tiles[x][y].region = next_region;
        }
        
        n_rooms += 1;
        next_region += 1;
        tries = 0;
        
        if (animate_make_rooms) {
            display();
            hilite_rect(r.x0, r.y0, r.x1, r.y1, color_from_name("green"));
            delay(125);        
        }
        
        try_again:
            ;;
    }
}


void walk(int x, int y, int dx, int dy) {
    if (animate_make_maze) {
        display(true);
        hilite_tile(x, y, color_from_name("green"));
    }

    tiles[x][y].kind = tk_floor;
    tiles[x][y].region = next_region;
    
    
    
    // std::vector<xy_t> ns;
    // if (dx == 0 && dy == 0) {
        // SELECT WHERE TO GO AT RANDOM WITH WEIGHTS
        //
        weighted_selector_t<xy_t> ns;
        
        static const xy_t dirs[] = { xy_t{-2,0}, xy_t{2,0}, xy_t{0,-2}, xy_t{0,2} };
        for (int i=0; i<4; ++i) {
            int nx = x+dirs[i].x;
            int ny = y+dirs[i].y;
            
            
            
            int weight = 1; 
            if (dirs[i].x == dx && dirs[i].y == dy) {
                // forward
                weight = 1;
            } else if (dirs[i].x == 0 && dx == dirs[i].y) {
                // right
                weight = 1;//10000;
            } else if (dirs[i].x == 0 && dx != dirs[i].y) {
                // left
                weight = 1;
            } else if (dirs[i].y == 0 && dy == dirs[i].x) {
                // left
                weight = 1;
            } else if (dirs[i].y == 0 && dy != dirs[i].x) {
                // right
                weight = 1;//10000;
            } 
            
            
            
            
            if (nx>=0 && nx<width && ny>=0 && ny<height) {
                tile_t nt = tiles[nx][ny];
                if (nt.kind == tk_wall && nt.room == -1) {
                    // ns.push_back(xy_t{nx, ny});
                    ns.push_back(xy_t{nx, ny}, weight);
                }
            }
        }
        
        if (!ns.empty()) {
            // xy_t n = ns[randrange(range_t{0, (int)ns.size()-1})];
            xy_t n = ns.select();
            
            int midx = (x+n.x)/2;
            int midy = (y+n.y)/2;
            tiles[midx][midy].kind = tk_floor;
            tiles[midx][midy].region = next_region;
            
            if (animate_make_maze) {
                hilite_tile(midx, midy, color_from_name("green"));
                delay(1);
            }
            
            walk(n.x, n.y, n.x-x, n.y-y);
        } else if (animate_make_maze) {
                delay(1);
        }
    // } // if (dx == 0 && dy == 0)
    
    // else {
        // ALWAYS KEEP GOING FORWARD IF POSSIBLE
        // PREFER RIGHT TURNS TO LEFT
        // xy_t f, r, l;
        // f = xy_t{dx, dy};
        // if (dy == 0) {
            // r = xy_t{0,dx};
            // l = xy_t{0,-dx};
        // }
        // else if (dx == 0) {
            // r = xy_t{-dy,0};
            // l = xy_t{dy, 0};
        // }
        // xy_t dirs[] = {f,r,l};
        // for (int i=0; i<3; ++i) {
            // int nx = x+dirs[i].x;
            // int ny = y+dirs[i].y;
            
            // if (nx>=0 && nx<width && ny>=0 && ny<height) {
                // tile_t nt = tiles[nx][ny];
                // if (nt.kind == tk_wall && nt.room == -1) {
                    // int midx = (x+nx)/2;
                    // int midy = (y+ny)/2;
                    // tiles[midx][midy].kind = tk_floor;
                    // tiles[midx][midy].region = next_region;
                    
                    // if (animate_make_maze) {
                        // hilite_tile(midx, midy, color_from_name("green"));
                        // delay(1);
                    // }                    
                    
                    // walk(nx, ny, nx-x, ny-y);
                    // return;
                // }
            // }
        // }
    // }
    
    
}

bool hunt(int& nextx, int& nexty) {
    for (int x=1; x<width;  x+=2) {
        if (animate_make_maze) {
            display(true);
            hilite_rect(x, 0, x, height-1, color_from_name("red"));
            delay(1);
        }
        
        
        for (int y=1; y<height; y+=2) {
            tile_t t = tiles[x][y];
            if ((t.kind == tk_wall) && (t.room == -1)) {
                static const xy_t dirs[] = { xy_t{-2,0}, xy_t{2,0}, xy_t{0,-2}, xy_t{0,2} };
                std::vector<xy_t> ns;
                
                for (int i=0; i<4; ++i) {
                    int nx = x+dirs[i].x;
                    int ny = y+dirs[i].y;
                    if (nx>=0 && nx<width && ny>=0 && ny<height) {
                        tile_t nt = tiles[nx][ny];
                        if (nt.kind == tk_floor && nt.room == -1) {
                            ns.push_back(xy_t{nx, ny});
                        }
                    }
                }
      
                
                if (ns.size() > 0) {
                    xy_t n = ns[randrange(range_t{0, (int)ns.size()-1})];
                    int midx = (x+n.x)/2;
                    int midy = (y+n.y)/2;
                    tiles[midx][midy].kind = tk_floor;
                    tiles[midx][midy].region = next_region;
                    nextx = x;
                    nexty = y;
                    
                    if (animate_make_maze) {
                        display(true);
                        hilite_tile(x, y, color_from_name("green"));
                        delay(125);
                    }
                    
                    return true;
                }
            }
        }
    }
    
    for (int x=1; x<width;  x+=2) {
        if (animate_make_maze) {
            display(true);
            hilite_rect(x, 0, x, height-1, color_from_name("red"));
            delay(1);        
        }
    
        for (int y=1; y<height; y+=2) {
        
        
            tile_t t = tiles[x][y];
            if ((t.kind == tk_wall) && (t.room == -1)) {   
                nextx = x;
                nexty = y;
                
                if (animate_make_maze) {
                    display(true);
                    hilite_tile(x, y, color_from_name("green"));
                    delay(125); 
                }
                
                next_region += 1;
                
                return true;
            }
        }
    }
    
    return false;
}

void make_maze() {
    int x, y;
    while (hunt(x, y)) 
        walk(x, y, 0, 0);
        
    /*
    while(true) {
        bool ok;
        benchmark("hunt phase", [&](){ ok = hunt(x, y); });
        if (!ok) break;
        benchmark("walk phase", [&](){ walk(x, y); });
    }
    */
}

void make_connections() {
    const int main_region = 0;
    
    struct connection_t {
        int x, y;
        int index;
        char region[2];
    };
    
    std::vector<connection_t> connections;
    
    for (int x=1; x<width-1; x+=1)
    for (int y=1; y<height-1; y+=1) {
        tile_t l,r,u,d;
        
        l = tiles[x-1][y];
        r = tiles[x+1][y];
        if (l.region != r.region && l.region >= 0 && r.region >= 0) {
            connection_t c;
            c.x = x;
            c.y = y;
            c.region[0] = std::min(l.region, r.region);
            c.region[1] = std::max(l.region, r.region);
            c.index = connections.size();
            connections.push_back(c);
            continue;
        }
        
        u = tiles[x][y-1];
        d = tiles[x][y+1];
        if (u.region != d.region && u.region >= 0 && d.region >= 0) {
            connection_t c;
            c.x = x;
            c.y = y;
            c.region[0] = std::min(u.region, d.region);
            c.region[1] = std::max(u.region, d.region);
            c.index = connections.size();
            connections.push_back(c);
        }
    }
    
    for (;;) {
        std::vector<connection_t> candidates;
        for (connection_t c: connections) {
            if (c.region[0] == main_region) {
                candidates.push_back(c);
            }
        }
        
        
        if (candidates.size() == 0) {
            break;
        }
        
        if (animate_make_connections) {
            display(true);
            for (connection_t c: candidates) {
                hilite_tile(c.x, c.y, color_from_name("green"));
            }   
            delay(125);
        }
        
        int i = randrange(range_t{0, (int)candidates.size()-1});
        connection_t conn = candidates[i];
        
        if (animate_make_connections) {
            display(true);
            for (connection_t c: candidates) {
                hilite_tile(c.x, c.y, color_from_name("green"));
            }   
            hilite_tile(conn.x, conn.y, color_from_name("red"));
            delay(125);        
        }
        
        
        tile_t& ct = tiles[conn.x][conn.y];
        ct.region = main_region;
        ct.kind = tk_floor;
        ct.door = true;
        
        for (int x=0; x<width; ++x)
        for (int y=0; y<height; ++y) {
            tile_t& t = tiles[x][y];
            if (t.region == conn.region[1]) {
                t.region = main_region;
            }
        }
                
        for (int i=connections.size()-1; i>=0; i--) {
            connection_t& c = connections[i];
            if (c.region[1] == conn.region[1]) {
                c.region[1] = main_region;
                std::swap(c.region[1], c.region[0]);
            }
            if (c.region[0] == conn.region[1]) {
                c.region[0] = main_region;
            }
            if (c.region[0] == c.region[1]) {
                std::swap(c, connections.back());
                connections.pop_back();
            }
        }       
    }
};

void remove_dead_ends() {
    int dead_ends_removed;
    do {
        dead_ends_removed = 0;
        
        for (int x=1; x<width-1; ++x)
        for (int y=1; y<height-1; ++y) {
            if (tiles[x][y].kind == tk_wall)
                continue;
            int floor_neighbours = 0;
            if (tiles[x-1][y].kind == tk_floor) floor_neighbours+=1;
            if (tiles[x+1][y].kind == tk_floor) floor_neighbours+=1;
            if (tiles[x][y-1].kind == tk_floor) floor_neighbours+=1;
            if (tiles[x][y+1].kind == tk_floor) floor_neighbours+=1;
            if (floor_neighbours == 1) {
                if (animate_remove_dead_ends) {
                    display();
                    hilite_tile(x, y, color_from_name("red"));
                    delay(1);
                }
                tiles[x][y].kind = tk_wall;
                dead_ends_removed += 1;
            }
        }
        
    } while (dead_ends_removed != 0);
}

int main() {
    srand(time(0));
    terminal_open();
    terminal_setf("window.size=%dx%d", width, height);
    // terminal_set("window.cellsize=16x16");
    
    for (;;) {
        init();
        // benchmark("making rooms", make_rooms);
        // benchmark("making the maze", make_maze);
        make_rooms();
     
        make_maze();

        make_connections();
        
        remove_dead_ends();
        
        display(false, false);
        wait_for_input();
        
    }
    
    terminal_close();
    
}