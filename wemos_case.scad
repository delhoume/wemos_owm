
height = 22.0;

thickness = 2;
fudge = 0.1;

width = 38.5;
depth = 30.50;
 
cavity_width = width - (2 * thickness);
cavity_depth = depth - (2 * thickness);
cavity_height = height - thickness + fudge;

hole_diameter = 0;

slit_width = 4.0;
slit_height = 23.0;

// lid
difference() {
    union () {
        translate([-(width / 2.0), 20, 0.0]) cube([width, depth, 2.0]);
        translate([-(cavity_width / 2.0), 22, 2.0]) cube([cavity_width, cavity_depth, 1.5]);            
    }
    translate([1, 22 + (cavity_depth / 2), 2.5]) cube(19,8, center = true);
    //rotate([0, 0, 90]) translate([21.9, -12.5, 0]) cube([slit_width, slit_height, 10]);
   // rotate([0, 0, 90]) translate([44.6, -12.5, 0]) cube([slit_width, slit_height, 10]);
}

// base
union() {
    translate([13.5, -(cavity_depth / 2.0), 2]) cube([4.0, cavity_depth, 4.0]);
    translate([-16, -(cavity_depth / 2.0), 2]) cube([4.0, cavity_depth, 2.0]);
    difference () {
        translate([0, 0, height / 2.0]) cube([width, depth, height], center = true); 
        translate([0, 0, (cavity_height / 2.0) + thickness]) cube([cavity_width, cavity_depth, cavity_height], center = true);
        translate([1, 0, thickness / 2.0]) cylinder(h = thickness + fudge, r1 = hole_diameter, r2 = hole_diameter, center = true);
        rotate([0, 0, 90]) translate([9, -12.5, 0]) cube([slit_width, slit_height, 5]);
        rotate([0, 0, 90]) translate([-13, -12.5, 0]) cube([slit_width, slit_height, 5]);
        translate([-20, -6, 1]) cube([3, 12, 8]);
        translate([-17.25, 13.0, 4]) cube([7, 3, 4]);        
    }
}