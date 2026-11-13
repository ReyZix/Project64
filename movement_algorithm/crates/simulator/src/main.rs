mod scene;
mod simulation;
mod ui;

use macroquad::prelude::*;
use scene::OrbitCamera;
use simulation::Simulation;
use ui::{Interface, Layout};

fn window_conf() -> Conf {
    Conf {
        window_title: "Project 64 | Movement Simulator".to_string(),
        window_width: 1320,
        window_height: 820,
        high_dpi: true,
        sample_count: 4,
        ..Default::default()
    }
}

#[macroquad::main(window_conf)]
async fn main() {
    let mut camera = OrbitCamera::new();
    let mut sim = Simulation::new();
    let mut ui = Interface::new();

    loop {
        let layout = Layout::new(screen_width(), screen_height());
        ui.handle_keyboard(&mut sim);
        camera.handle_mouse(layout.viewport(), &mut sim);
        sim.update(get_frame_time());

        clear_background(ui::BACKGROUND);
        set_camera(&layout.camera());
        ui.draw_workspace(&layout);
        let view = camera.view(layout.viewport());
        set_camera(&view);
        scene::draw(
            &sim,
            ui.show_clearance,
            ui.show_route,
            view.position.y < 0.0,
        );
        set_camera(&layout.camera());
        ui.draw(&layout, &view, &mut camera, &mut sim);

        next_frame().await;
    }
}
